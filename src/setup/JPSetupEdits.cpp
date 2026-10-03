// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPSetupEdits.h"

#include "JPSetupTree.h"

#include <algorithm>
#include <cstdio>
#include <random>

inline namespace jf {

namespace {

// Every id in the cell, whatever it names.
bool idTaken(const JPCellConfig& cell, const std::string& id) {
    auto in = [&id](const auto& items) {
        return std::any_of(items.begin(), items.end(), [&id](const auto& i) { return i.id == id; });
    };
    return in(cell.drivers) || in(cell.axes) || in(cell.heads) || in(cell.nozzles) || in(cell.nozzleTips)
        || in(cell.cameras) || in(cell.actuators);
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
    if (g.id == "drivers")   return "Controller";
    if (g.id == "axes")      return "Axis";
    if (g.id == "heads")     return "Head";
    if (g.id == "nozzles")   return "Nozzle";
    if (g.id == "nozzletips") return "Nozzle Tip";
    if (g.id == "cameras")   return "Camera";
    if (g.id == "actuators") return "Actuator";
    return {};
}

std::string JPSetupEdits::add(JPCellConfig& cell, const std::string& path) {
    const JPSetupTree::Path g = JPSetupTree::parse(JPSetupTree::groupOf(cell, path));
    if (g.kind != "group") return {};
    if (g.id == "drivers") {
        JPDriverConfig d;
        d.id = newId(cell, "DRV");
        d.name = "New controller";
        d.link = JJson::object();
        d.link["type"] = "serial";
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
        n.mount.headId = g.headId;
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
        c.looksUp = g.headId.empty();   // a camera fixed to the machine looks up at the nozzles
        c.mount.headId = g.headId;
        c.device = JJson::object();
        c.device["backend"] = "v4l2";
        cell.cameras.push_back(c);
        return "camera:" + c.id;
    }
    if (g.id == "actuators") {
        JPActuatorConfig a;
        a.id = newId(cell, "ACT");
        a.name = "New actuator";
        a.mount.headId = g.headId;
        if (!cell.drivers.empty()) a.driverId = cell.drivers.front().id;
        cell.actuators.push_back(a);
        return "actuator:" + a.id;
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
            if (a.kind == JPAxisConfig::Kind::Mapped && a.inputAxisId == p.id) users.push_back("axis " + a.name);
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
    } else if (p.kind == "nozzletip") {
        for (const JPNozzleConfig& n : cell.nozzles)
            if (n.tipId == p.id) users.push_back("nozzle " + n.name + " (it is on it)");
    } else if (p.kind != "nozzle" && p.kind != "camera") {
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
                                               : erase(cell.actuators, p.id);
    if (!removed) {
        why = "it is not in this cell";
        return false;
    }
    if (p.kind == "nozzletip")
        for (JPNozzleConfig& n : cell.nozzles) std::erase(n.tipIds, p.id);
    return true;
}

bool JPSetupEdits::move(JPCellConfig& cell, const std::string& path, int by) {
    const JPSetupTree::Path p = JPSetupTree::parse(path);
    auto any = [](const auto&, const auto&) { return true; };
    auto sameHead = [](const auto& a, const auto& b) { return a.mount.headId == b.mount.headId; };
    if (p.kind == "driver")   return moveIn(cell.drivers, p.id, by, any);
    if (p.kind == "axis")     return moveIn(cell.axes, p.id, by, any);
    if (p.kind == "head")     return moveIn(cell.heads, p.id, by, any);
    if (p.kind == "nozzle")   return moveIn(cell.nozzles, p.id, by, sameHead);
    if (p.kind == "nozzletip") return moveIn(cell.nozzleTips, p.id, by, any);
    if (p.kind == "camera")   return moveIn(cell.cameras, p.id, by, sameHead);
    if (p.kind == "actuator") return moveIn(cell.actuators, p.id, by, sameHead);
    return false;
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
