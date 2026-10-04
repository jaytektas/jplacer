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
    if (!squareness.axisX.empty()) j["squareness"] = squareness.toJson();
    if (parkAfterHome) j["parkAfterHome"] = true;
    if (discardLocation) j["discardLocation"] = discardLocation->toJson();
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
    c.squareness = JPSquarenessConfig::fromJson(j["squareness"]);
    // Kept on the machine before it was a controller's: every controller's now.
    if (j["homeAfterConnect"].boolean())
        for (JPDriverConfig& d : c.drivers) d.homeAfterConnect = true;
    c.parkAfterHome = j["parkAfterHome"].boolean();
    c.discardLocation = JPMachineLocation::fromJson(j["discardLocation"]);
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

std::vector<std::string> JPCellConfig::problems() const {
    std::vector<std::string> out;
    std::set<std::string> headIds;
    for (const JPHeadConfig& h : heads) headIds.insert(h.id);

    for (const JPAxisConfig& a : axes) {
        if (a.kind == JPAxisConfig::Kind::Controller && !driver(a.driverId))
            out.push_back("axis " + a.name + " names a controller that is not in this cell");
        if (a.kind == JPAxisConfig::Kind::Mapped) {
            if (!axis(a.inputAxisId))     out.push_back("axis " + a.name + " follows an axis that is not in this cell");
            else if (!a.mapped(0))        out.push_back("axis " + a.name + " has a map with both points at the same input");
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
        if ((a.canSwitch() || a.canRead()) && !driver(a.driverId))
            out.push_back("actuator " + a.name + " has commands but no controller to send them to");
    }
    for (const JPHeadConfig& h : heads)
        if (!h.pumpActuatorId.empty() && !actuatorIds.count(h.pumpActuatorId))
            out.push_back("head " + h.name + " names a pump actuator that is not in this cell");
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
