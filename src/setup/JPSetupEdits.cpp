// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPSetupEdits.h"

#include "JPSetupTree.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <random>

inline namespace jf {

namespace {

// Every id in the cell, whatever it names.
bool idTaken(const JPCellConfig& cell, const std::string& id) {
    auto in = [&id](const auto& items) {
        return std::any_of(items.begin(), items.end(), [&id](const auto& i) { return i.id == id; });
    };
    return in(cell.drivers) || in(cell.axes) || in(cell.heads) || in(cell.nozzles) || in(cell.nozzleTips)
        || in(cell.cameras) || in(cell.actuators) || in(cell.signalers);
}

// Whether a mount moves on `axisId`.
bool mountUses(const JPMountConfig& m, const std::string& axisId) {
    return m.axisX == axisId || m.axisY == axisId || m.axisZ == axisId || m.axisRotation == axisId;
}

template <class T>
bool erase(std::vector<T>& items, const std::string& id) {
    const auto it = std::find_if(items.begin(), items.end(), [&id](const T& i) { return i.id == id; });
    if (it == items.end()) return false;
    items.erase(it);
    return true;
}

// Swap the part `id` with the next one in the same group (`same`) that way.
template <class T, class Same>
bool moveIn(std::vector<T>& items, const std::string& id, int by, Same same) {
    const auto at = std::find_if(items.begin(), items.end(), [&id](const T& i) { return i.id == id; });
    if (at == items.end()) return false;
    const long i = at - items.begin();
    for (long j = i + by; j >= 0 && j < long(items.size()); j += by)
        if (same(items[size_t(j)], items[size_t(i)])) {
            std::swap(items[size_t(i)], items[size_t(j)]);
            return true;
        }
    return false;
}

// A tip's list of steps a step path names, when it can be changed (not
// unloading that is loading backwards); null otherwise.
template <class Cell>
auto stepList(Cell& cell, const JPSetupTree::Path& p) -> decltype(&cell.nozzleTips.front().loadSteps) {
    for (auto& t : cell.nozzleTips) {
        if (t.id != p.owner) continue;
        if (p.list == "load") return &t.loadSteps;
        if (p.list == "unload" && !t.unloadReversesLoad) return &t.unloadSteps;
    }
    return nullptr;
}

// A step's index, or -1.
long stepIndex(const JPSetupTree::Path& p, const std::vector<JPChangerStep>& steps) {
    char* end = nullptr;
    const long i = std::strtol(p.id.c_str(), &end, 10);
    return end && *end == '\0' && !p.id.empty() && i >= 0 && i < long(steps.size()) ? i : -1;
}

std::string stepPath(const JPSetupTree::Path& p, long index) {
    return "step:" + p.owner + ":" + p.list + ":" + std::to_string(index);
}

// "a, b and c"
std::string listed(const std::vector<std::string>& names) {
    std::string out;
    for (size_t i = 0; i < names.size(); ++i)
        out += (i == 0 ? "" : i + 1 == names.size() ? " and " : ", ") + names[i];
    return out;
}

} // namespace

std::string JPSetupEdits::addable(const JPCellConfig& cell, const std::string& path) {
    const JPSetupTree::Path g = JPSetupTree::parse(JPSetupTree::groupOf(cell, path));
    if (g.kind != "group") return {};
    if (g.id == "load" || g.id == "unload") {
        JPSetupTree::Path list{ "step", "", g.owner, g.id };
        return stepList(cell, list) ? "Step" : "";
    }
    if (g.id == "drivers")   return "Controller";
    if (g.id == "axes")      return "Axis";
    if (g.id == "heads")     return "Head";
    if (g.id == "nozzles")   return "Nozzle";
    if (g.id == "nozzletips") return "Nozzle Tip";
    if (g.id == "cameras")   return "Camera";
    if (g.id == "actuators") return "Actuator";
    if (g.id == "signalers") return "Signaler";
    return {};
}

std::vector<std::string> JPSetupEdits::kinds(const JPCellConfig& cell, const std::string& path) {
    const JPSetupTree::Path g = JPSetupTree::parse(JPSetupTree::groupOf(cell, path));
    if (g.kind == "group" && g.id == "signalers") return JPSignalerConfig::classNames();
    // OpenPnP's driver classes: jplacer's simulated controller, a G-code one (GcodeAsyncDriver too: jplacer's
    // controllers queue their commands either way), or a NeoDen 4.
    if (g.kind == "group" && g.id == "drivers") return { "NullDriver", "GcodeDriver", "GcodeAsyncDriver", "NeoDen4Driver" };
    if (g.kind == "group" && g.id == "actuators") return { "ReferenceActuator", "HttpActuator", "ScriptActuator", "ThermistorToLinearSensorActuator", "NeoDen4FeederActuator" };
    if (g.kind == "group" && g.id == "cameras")
        return { "OpenPnpCaptureCamera", "Neoden4Camera", "Neoden4SwitcherCamera", "MjpgCaptureCamera", "ImageCamera",
                 "SwitcherCamera", "OnvifIPCamera", "GstreamerCamera" };
    return {};
}

std::string JPSetupEdits::add(JPCellConfig& cell, const std::string& path, const std::string& kind) {
    const JPSetupTree::Path g = JPSetupTree::parse(JPSetupTree::groupOf(cell, path));
    if (g.kind != "group") return {};
    if (g.id == "load" || g.id == "unload") {
        const JPSetupTree::Path list{ "step", "", g.owner, g.id };
        std::vector<JPChangerStep>* steps = stepList(cell, list);
        if (!steps) return {};
        const JPSetupTree::Path p = JPSetupTree::parse(path);
        const long after = p.kind == "step" ? stepIndex(p, *steps) : -1;
        const long at = after >= 0 ? after + 1 : long(steps->size());
        steps->insert(steps->begin() + at, JPChangerStep{});
        return stepPath(list, at);
    }
    if (g.id == "drivers") {
        if (kind != "NullDriver" && kind != "GcodeDriver" && kind != "GcodeAsyncDriver" && kind != "NeoDen4Driver") return {};
        JPDriverConfig d;
        d.id = newId(cell, "DRV");
        d.name = kind;   // as OpenPnP names a new one: its class
        d.link = JJson::object();
        d.link["type"] = kind == "NullDriver" ? "simulated" : kind == "NeoDen4Driver" ? "neoden4" : "serial";
        if (kind == "NeoDen4Driver") d.profile = "neoden4";
        cell.drivers.push_back(d);
        return "driver:" + d.id;
    }
    if (g.id == "axes") {
        JPAxisConfig a;
        a.id = newId(cell, "AXS");
        a.name = "New axis";
        if (!cell.drivers.empty()) a.driverId = cell.drivers.front().id;
        cell.axes.push_back(a);
        return "axis:" + a.id;
    }
    if (g.id == "heads") {
        JPHeadConfig h;
        h.id = newId(cell, "HED");
        h.name = "New head";
        cell.heads.push_back(h);
        return "head:" + h.id;
    }
    if (g.id == "nozzles") {
        JPNozzleConfig n;
        n.id = newId(cell, "NOZ");
        n.name = "New nozzle";
        n.mount.headId = g.owner;
        cell.nozzles.push_back(n);
        return "nozzle:" + n.id;
    }
    if (g.id == "nozzletips") {
        JPNozzleTipConfig t;
        t.id = newId(cell, "TIP");
        t.name = "New nozzle tip";
        cell.nozzleTips.push_back(t);
        return "nozzletip:" + t.id;
    }
    if (g.id == "cameras") {
        JPCameraConfig c;
        c.id = newId(cell, "CAM");
        c.name = "New camera";
        c.looksUp = g.owner.empty();   // a camera fixed to the machine looks up at the nozzles
        c.mount.headId = g.owner;
        c.device = JJson::object();
        // OpenPnP's ImageCamera (a picture of the table) when chosen, else a capture device.
        c.device["backend"] = kind == "ImageCamera"             ? "image"
                              : kind == "MjpgCaptureCamera"     ? "mjpg"
                              : kind == "SwitcherCamera"        ? "switcher"
                              : kind == "OnvifIPCamera"         ? "onvif"
                              : kind == "GstreamerCamera"       ? "gstreamer"
                              : kind == "Neoden4Camera"         ? "neoden4"
                              : kind == "Neoden4SwitcherCamera" ? "neoden4Switcher"
                                                                : "v4l2";
        cell.cameras.push_back(c);
        return "camera:" + c.id;
    }
    if (g.id == "actuators") {
        JPActuatorConfig a;
        a.id = newId(cell, "ACT");
        a.name = "New actuator";
        a.http.on = kind == "HttpActuator";
        if (kind == "ScriptActuator") a.scriptName = "Actuators/" + a.id + ".py";
        a.thermistor.on = kind == "ThermistorToLinearSensorActuator";
        a.mount.headId = g.owner;
        if (!cell.drivers.empty()) a.driverId = cell.drivers.front().id;
        // OpenPnP's NeoDen4FeederActuator: set to a length, on the NeoDen 4 controller.
        if (kind == "NeoDen4FeederActuator") {
            a.neoden4Feeder.on = true;
            a.valueType = JPActuatorConfig::ValueType::Number;
            for (const JPDriverConfig& d : cell.drivers)
                if (std::as_const(d.link)["type"].str() == "neoden4") {
                    a.driverId = d.id;
                    break;
                }
        }
        cell.actuators.push_back(a);
        return "actuator:" + a.id;
    }
    if (g.id == "signalers") {
        const auto& names = JPSignalerConfig::classNames();
        const auto it = std::find(names.begin(), names.end(), kind);
        if (it == names.end()) return {};
        JPSignalerConfig s;
        s.kind = JPSignalerConfig::Kind(it - names.begin());
        s.id = newId(cell, "SIG");
        s.name = kind;   // as OpenPnP names a new one: its class
        cell.signalers.push_back(s);
        return "signaler:" + s.id;
    }
    return {};
}

bool JPSetupEdits::remove(JPCellConfig& cell, const std::string& path, std::string& why) {
    const JPSetupTree::Path p = JPSetupTree::parse(path);
    std::vector<std::string> users;
    if (p.kind == "driver") {
        for (const JPAxisConfig& a : cell.axes)
            if (a.kind == JPAxisConfig::Kind::Controller && a.driverId == p.id) users.push_back("axis " + a.name);
        for (const JPActuatorConfig& a : cell.actuators)
            if (a.driverId == p.id) users.push_back("actuator " + a.name);
    } else if (p.kind == "axis") {
        for (const JPAxisConfig& a : cell.axes)
            if (a.transformed() && a.inputAxisId == p.id) users.push_back("axis " + a.name);
        for (const JPNozzleConfig& n : cell.nozzles)
            if (mountUses(n.mount, p.id)) users.push_back("nozzle " + n.name);
        for (const JPCameraConfig& c : cell.cameras)
            if (mountUses(c.mount, p.id)) users.push_back("camera " + c.name);
        for (const JPActuatorConfig& a : cell.actuators)
            if (mountUses(a.mount, p.id)) users.push_back("actuator " + a.name);
        if (cell.squareness.axisX == p.id || cell.squareness.axisY == p.id) users.push_back("the squareness correction");
    } else if (p.kind == "head") {
        for (const JPNozzleConfig& n : cell.nozzles)
            if (n.mount.headId == p.id) users.push_back("nozzle " + n.name);
        for (const JPCameraConfig& c : cell.cameras)
            if (c.mount.headId == p.id) users.push_back("camera " + c.name);
        for (const JPActuatorConfig& a : cell.actuators)
            if (a.mount.headId == p.id) users.push_back("actuator " + a.name);
    } else if (p.kind == "actuator") {
        for (const JPCameraConfig& c : cell.cameras)
            if (c.lightActuator() == p.id) users.push_back("camera " + c.name + " (its light)");
        for (const JPNozzleConfig& n : cell.nozzles)
            if (n.vacuumActuatorId == p.id) users.push_back("nozzle " + n.name + " (its vacuum)");
        for (const JPHeadConfig& h : cell.heads)
            if (h.pumpActuatorId == p.id) users.push_back("head " + h.name + " (its pump)");
        for (const JPHeadConfig& h : cell.heads)
            if (h.zProbeActuatorId == p.id) users.push_back("head " + h.name + " (its Z probe)");
        for (const JPSignalerConfig& s : cell.signalers)
            if (s.kind == JPSignalerConfig::Kind::Actuator && s.actuatorId == p.id) users.push_back("signaler " + s.name);
        for (const JPActuatorConfig& a : cell.actuators)
            if (std::find(a.profileActuators.begin(), a.profileActuators.end(), p.id) != a.profileActuators.end())
                users.push_back("actuator " + a.name + " (its profiles)");
    } else if (p.kind == "step") {
        std::vector<JPChangerStep>* steps = stepList(cell, p);
        const long i = steps ? stepIndex(p, *steps) : -1;
        if (i < 0) {
            why = steps ? "it is not in this cell" : "unloading is loading backwards: change loading, or give it steps of its own";
            return false;
        }
        steps->erase(steps->begin() + i);
        return true;
    } else if (p.kind == "nozzletip") {
        for (const JPNozzleConfig& n : cell.nozzles)
            if (n.tipId == p.id) users.push_back("nozzle " + n.name + " (it is on it)");
    } else if (p.kind != "nozzle" && p.kind != "camera" && p.kind != "signaler") {
        why = "only a part can be removed";
        return false;
    }
    if (!users.empty()) {
        why = listed(users) + (users.size() == 1 ? " uses it" : " use it");
        return false;
    }
    const bool removed = p.kind == "driver"    ? erase(cell.drivers, p.id)
                       : p.kind == "axis"      ? erase(cell.axes, p.id)
                       : p.kind == "head"      ? erase(cell.heads, p.id)
                       : p.kind == "nozzle"    ? erase(cell.nozzles, p.id)
                       : p.kind == "nozzletip" ? erase(cell.nozzleTips, p.id)
                       : p.kind == "camera"    ? erase(cell.cameras, p.id)
                       : p.kind == "signaler"  ? erase(cell.signalers, p.id)
                                               : erase(cell.actuators, p.id);
    if (!removed) {
        why = "it is not in this cell";
        return false;
    }
    if (p.kind == "nozzletip")
        for (JPNozzleConfig& n : cell.nozzles) std::erase(n.tipIds, p.id);
    return true;
}

std::string JPSetupEdits::move(JPCellConfig& cell, const std::string& path, int by) {
    const JPSetupTree::Path p = JPSetupTree::parse(path);
    if (p.kind == "step") {
        std::vector<JPChangerStep>* steps = stepList(cell, p);
        const long i = steps ? stepIndex(p, *steps) : -1, j = i + by;
        if (i < 0 || j < 0 || j >= long(steps->size())) return {};
        std::swap((*steps)[size_t(i)], (*steps)[size_t(j)]);
        return stepPath(p, j);
    }
    auto any = [](const auto&, const auto&) { return true; };
    auto sameHead = [](const auto& a, const auto& b) { return a.mount.headId == b.mount.headId; };
    const bool moved = p.kind == "driver"    ? moveIn(cell.drivers, p.id, by, any)
                     : p.kind == "axis"      ? moveIn(cell.axes, p.id, by, any)
                     : p.kind == "head"      ? moveIn(cell.heads, p.id, by, any)
                     : p.kind == "nozzle"    ? moveIn(cell.nozzles, p.id, by, sameHead)
                     : p.kind == "nozzletip" ? moveIn(cell.nozzleTips, p.id, by, any)
                     : p.kind == "camera"    ? moveIn(cell.cameras, p.id, by, sameHead)
                     : p.kind == "actuator"  ? moveIn(cell.actuators, p.id, by, sameHead)
                     : p.kind == "signaler"  ? moveIn(cell.signalers, p.id, by, any)
                                             : false;
    return moved ? path : std::string();
}

std::string JPSetupEdits::newId(const JPCellConfig& cell, const std::string& prefix) {
    // As OpenPnP makes them: a kind and sixteen hex digits, so a part keeps
    // its id whatever it is renamed to.
    static std::mt19937_64 rng{ std::random_device{}() };
    for (;;) {
        char buf[32];
        std::snprintf(buf, sizeof buf, "%s%016llx", prefix.c_str(), static_cast<unsigned long long>(rng()));
        if (!idTaken(cell, buf)) return buf;
    }
}

} // inline namespace jf
