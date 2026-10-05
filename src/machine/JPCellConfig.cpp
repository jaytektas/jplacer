// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPCellConfig.h"

#include <filesystem>
#include <set>

inline namespace jf {

namespace {

template <class T>
JJson toArray(const std::vector<T>& items) {
    JJson a = JJson::array();
    for (const T& i : items) a.push(i.toJson());
    return a;
}

} // namespace

JJson JPCellConfig::toJson() const {
    JJson j = JJson::object();
    j["name"]      = name;
    j["drivers"]   = toArray(drivers);
    j["heads"]     = toArray(heads);
    j["axes"]      = toArray(axes);
    j["nozzles"]   = toArray(nozzles);
    j["nozzleTips"] = toArray(nozzleTips);
    j["cameras"]   = toArray(cameras);
    j["actuators"] = toArray(actuators);
    if (!signalers.empty()) j["signalers"] = toArray(signalers);
    if (!squareness.axisX.empty()) j["squareness"] = squareness.toJson();
    if (parkAfterHome) j["parkAfterHome"] = true;
    if (discardLocation) j["discardLocation"] = discardLocation->toJson();
    j["defaultBoardLocation"] = defaultBoardLocation.toJson();
    j["autoToolSelect"] = autoToolSelect;
    j["safeZPark"] = safeZPark;
    j["unsafeZRoaming"] = unsafeZRoamingMm;
    j["autoLoadMostRecentJob"] = autoLoadMostRecentJob;
    j["jobProcessor"] = jobProcessor.toJson();
    j["vision"] = vision.toJson();
    return j;
}

bool JPCellConfig::fromJson(const JJson& j, std::string& error) {
    JPCellConfig c;
    c.name = j["name"].str();
    for (const JJson& d : j["drivers"].arr()) {
        auto cfg = JPDriverConfig::fromJson(d, error);
        if (!cfg) return false;
        c.drivers.push_back(std::move(*cfg));
    }
    for (const JJson& a : j["axes"].arr()) {
        auto cfg = JPAxisConfig::fromJson(a, error);
        if (!cfg) return false;
        c.axes.push_back(std::move(*cfg));
    }
    for (const JJson& h : j["heads"].arr())     c.heads.push_back(JPHeadConfig::fromJson(h));
    for (const JJson& n : j["nozzles"].arr())   c.nozzles.push_back(JPNozzleConfig::fromJson(n));
    for (const JJson& t : j["nozzleTips"].arr()) c.nozzleTips.push_back(JPNozzleTipConfig::fromJson(t));
    for (const JJson& m : j["cameras"].arr())   c.cameras.push_back(JPCameraConfig::fromJson(m));
    for (const JJson& a : j["actuators"].arr()) c.actuators.push_back(JPActuatorConfig::fromJson(a));
    for (const JJson& s : j["signalers"].arr()) c.signalers.push_back(JPSignalerConfig::fromJson(s));
    c.squareness = JPSquarenessConfig::fromJson(j["squareness"]);
    // Kept on the machine before it was a controller's: every controller's now.
    if (j["homeAfterConnect"].boolean())
        for (JPDriverConfig& d : c.drivers) d.homeAfterConnect = true;
    c.parkAfterHome = j["parkAfterHome"].boolean();
    c.discardLocation = JPMachineLocation::fromJson(j["discardLocation"]);
    c.defaultBoardLocation = JPMachineLocation::fromJson(j["defaultBoardLocation"]).value_or(JPMachineLocation {});
    if (j["autoToolSelect"].isBool()) c.autoToolSelect = j["autoToolSelect"].boolean();
    if (j["safeZPark"].isBool()) c.safeZPark = j["safeZPark"].boolean();
    c.unsafeZRoamingMm = j["unsafeZRoaming"].number(c.unsafeZRoamingMm);
    if (j["autoLoadMostRecentJob"].isBool()) c.autoLoadMostRecentJob = j["autoLoadMostRecentJob"].boolean();
    c.jobProcessor = JPJobProcessorConfig::fromJson(j["jobProcessor"]);
    c.vision = JPVisionConfig::fromJson(j["vision"]);
    *this = std::move(c);
    return true;
}

bool JPCellConfig::load(const std::string& path, std::string& error) {
    const std::optional<JJson> doc = JJson::tryParseFile(path);
    if (!doc || !doc->isObject()) {
        error = path + ": not a JSON object";
        return false;
    }
    if (!fromJson(*doc, error)) {
        error = path + ": " + error;
        return false;
    }
    return true;
}

bool JPCellConfig::save(const std::string& path, std::string& error) const {
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(path).parent_path(), ec);
    if (!toJson().dumpToFile(path)) {
        error = path + ": could not be written";
        return false;
    }
    return true;
}

const JPAxisConfig* JPCellConfig::axis(const std::string& id) const {
    for (const JPAxisConfig& a : axes) if (a.id == id) return &a;
    return nullptr;
}

const JPDriverConfig* JPCellConfig::driver(const std::string& id) const {
    for (const JPDriverConfig& d : drivers) if (d.id == id) return &d;
    return nullptr;
}

const JPActuatorConfig* JPCellConfig::actuatorNamed(const std::string& name) const {
    for (const bool onHead : { true, false })
        for (const JPActuatorConfig& a : actuators)
            if (a.name == name && a.mount.headId.empty() != onHead) return &a;
    for (const JPActuatorConfig& a : actuators)
        if (a.id == name) return &a;
    return nullptr;
}

std::vector<std::string> JPCellConfig::problems() const {
    std::vector<std::string> out;
    std::set<std::string> headIds;
    for (const JPHeadConfig& h : heads) headIds.insert(h.id);

    for (const JPDriverConfig& d : drivers)
        if (d.supportingPreMove && d.usingLetterVariables)
            out.push_back("controller " + d.name + " allows pre-move commands with letter variables on: turn one off");
    for (const JPAxisConfig& a : axes) {
        if (a.kind == JPAxisConfig::Kind::Controller && !driver(a.driverId))
            out.push_back("axis " + a.name + " names a controller that is not in this cell");
        if (a.transformed()) {
            if (!axis(a.inputAxisId))     out.push_back("axis " + a.name + " follows an axis that is not in this cell");
            else if (!a.mapped(0))        out.push_back("axis " + a.name + " has a map with both points at the same input");
            else if (a.kind == JPAxisConfig::Kind::Cam && a.camRadius <= 0)
                out.push_back("axis " + a.name + " is a cam with no radius");
        }
    }
    auto checkMount = [&](const std::string& what, const JPMountConfig& m) {
        if (!m.headId.empty() && !headIds.count(m.headId)) out.push_back(what + " is on a head that is not in this cell");
        for (const std::string* id : { &m.axisX, &m.axisY, &m.axisZ, &m.axisRotation })
            if (!id->empty() && !axis(*id)) out.push_back(what + " names an axis that is not in this cell");
    };
    std::set<std::string> actuatorIds;
    for (const JPActuatorConfig& a : actuators) {
        actuatorIds.insert(a.id);
        checkMount("actuator " + a.name, a.mount);
        // A profile actuator sends nothing of its own: its actuators do.
        const bool sends = !a.http.on && a.scriptName.empty() && ((a.valueType != JPActuatorConfig::ValueType::Profile && a.canSwitch()) || a.canRead());
        if (sends && !driver(a.driverId))
            out.push_back("actuator " + a.name + " has commands but no controller to send them to");
    }
    for (const JPActuatorConfig& a : actuators)
        for (const std::string& id : a.profileActuators)
            if (!id.empty() && !actuatorIds.count(id))
                out.push_back("actuator " + a.name + " has a profile actuator that is not in this cell");
    for (const JPHeadConfig& h : heads)
        if (!h.pumpActuatorId.empty() && !actuatorIds.count(h.pumpActuatorId))
            out.push_back("head " + h.name + " names a pump actuator that is not in this cell");
    for (const JPSignalerConfig& s : signalers)
        if (s.kind == JPSignalerConfig::Kind::Actuator && !s.actuatorId.empty() && !actuatorIds.count(s.actuatorId))
            out.push_back("signaler " + s.name + " switches an actuator that is not in this cell");
    std::set<std::string> tipIds;
    for (const JPNozzleTipConfig& t : nozzleTips) tipIds.insert(t.id);
    for (const JPNozzleConfig& n : nozzles) {
        checkMount("nozzle " + n.name, n.mount);
        for (const auto& [id, what] : { std::pair{ &n.vacuumActuatorId, "vacuum" }, std::pair{ &n.blowOffActuatorId, "blow-off" },
                                        std::pair{ &n.vacuumSenseActuatorId, "vacuum sensing" } })
            if (!id->empty() && !actuatorIds.count(*id))
                out.push_back("nozzle " + n.name + " names a " + what + " actuator that is not in this cell");
        for (const std::string& t : n.tipIds)
            if (!tipIds.count(t)) out.push_back("nozzle " + n.name + " fits a nozzle tip that is not in this cell");
        if (!n.tipId.empty() && !n.fits(n.tipId))
            out.push_back("nozzle " + n.name + " has a nozzle tip on it that does not fit it");
    }
    for (const JPNozzleTipConfig& t : nozzleTips) {
        for (const std::string& p : t.problems()) out.push_back(p);
        for (const auto* steps : { &t.loadSteps, &t.unloadSteps })
            for (const JPChangerStep& s : *steps)
                if (s.kind == JPChangerStep::Kind::Actuator && !s.actuatorId.empty() && !actuatorIds.count(s.actuatorId))
                    out.push_back("nozzle tip " + t.name + " switches an actuator that is not in this cell");
    }
    for (const JPCameraConfig& c : cameras) checkMount("camera " + c.name, c.mount);
    if (!squareness.axisX.empty() || !squareness.axisY.empty())
        for (const std::string* id : { &squareness.axisX, &squareness.axisY }) {
            const JPAxisConfig* a = axis(*id);
            if (!a || a->kind != JPAxisConfig::Kind::Controller)
                out.push_back("the squareness correction names an axis that is not a controller's in this cell");
        }
    return out;
}

} // inline namespace jf
