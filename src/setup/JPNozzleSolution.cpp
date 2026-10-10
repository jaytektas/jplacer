// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPNozzleSolution.h"

#include "JPSetupEdits.h"

#include <algorithm>
#include <deque>
#include <set>

inline namespace jf {

namespace {

// OpenPnP's new rotation axis: limited to ±180°, 200000°/min, accelerating to it in half a second.
constexpr double kRotationLimitDeg = 180, kRotationFeedPerS = 200000.0 / 60, kRotationAccelerationFactor = 2;

// The controller axis an axis comes down to (its input, for a mapped or cam axis).
const JPAxisConfig* rawAxis(const JPCellConfig& cell, const std::string& id) {
    const JPAxisConfig* a = cell.axis(id);
    for (int depth = 0; a && a->transformed() && depth < 4; ++depth) a = cell.axis(a->inputAxisId);
    return a;
}

JPAxisConfig* axisOf(JPCellConfig& cell, const std::string& id) {
    for (JPAxisConfig& a : cell.axes)
        if (a.id == id) return &a;
    return nullptr;
}

template <class T>
T take(std::deque<T>& from) {
    T first = from.front();
    from.pop_front();
    return first;
}

} // namespace

const char* JPNozzleSolution::name(Kind k) {
    switch (k) {
        case Kind::Standalone:  return "Standalone";
        case Kind::DualNegated: return "DualNegated";
        case Kind::DualCam:     return "DualCam";
    }
    return "Standalone";
}

bool JPNozzleSolution::parse(const std::string& name, Kind& kind) {
    for (Kind k : { Kind::Standalone, Kind::DualNegated, Kind::DualCam })
        if (name == JPNozzleSolution::name(k)) {
            kind = k;
            return true;
        }
    return false;
}

void JPNozzleSolution::current(const JPCellConfig& cell, const std::string& headId, Kind& kind, int& units) {
    kind = Kind::Standalone;
    units = 0;
    for (const JPNozzleConfig& n : cell.nozzles) {
        if (n.mount.headId != headId) continue;
        const JPAxisConfig* z = cell.axis(n.mount.axisZ);
        if (!z) continue;
        if (z->kind == JPAxisConfig::Kind::Cam) {
            // One unit for each pair: counted by its counter-clockwise half.
            if (!z->camClockwise) ++units;
            kind = Kind::DualCam;
        } else if (z->kind == JPAxisConfig::Kind::Mapped) {
            kind = Kind::DualNegated;   // counted by its controller axis partner
        } else if (z->kind == JPAxisConfig::Kind::Controller) {
            ++units;
        }
    }
    units = std::max(units, 1);
}

void JPNozzleSolution::apply(JPCellConfig& cell, const std::string& headId, const std::string& cameraId, Kind kind, int units) {
    const JPCameraConfig* camera = nullptr;
    for (const JPCameraConfig& c : cell.cameras)
        if (c.id == cameraId) camera = &c;
    const std::string axisX = camera ? camera->mount.axisX : std::string(), axisY = camera ? camera->mount.axisY : std::string();
    const JPAxisConfig* cameraX = cell.axis(axisX);
    const std::string driverId = cameraX ? cameraX->driverId : (cell.drivers.empty() ? std::string() : cell.drivers.front().id);

    // What there is now, to be reused in order.
    std::deque<std::string> nozzles, axesZ, axesC, negated, camsCcw, camsCw, valves, senses;
    auto once = [](std::deque<std::string>& list, const std::string& id) {
        if (!id.empty() && std::find(list.begin(), list.end(), id) == list.end()) list.push_back(id);
    };
    for (const JPNozzleConfig& n : cell.nozzles) {
        if (n.mount.headId != headId) continue;
        once(nozzles, n.id);
        if (const JPAxisConfig* raw = rawAxis(cell, n.mount.axisZ)) once(axesZ, raw->id);
        if (const JPAxisConfig* raw = rawAxis(cell, n.mount.axisRotation)) once(axesC, raw->id);
        if (const JPAxisConfig* z = cell.axis(n.mount.axisZ)) {
            if (z->kind == JPAxisConfig::Kind::Mapped) once(negated, z->id);
            if (z->kind == JPAxisConfig::Kind::Cam) once(z->camClockwise ? camsCw : camsCcw, z->id);
        }
        once(senses, n.vacuumSenseActuatorId);
        // A valve shared with the sensing made an actuator of its own.
        if (n.vacuumActuatorId != n.vacuumSenseActuatorId) once(valves, n.vacuumActuatorId);
    }

    auto nozzle = [&](const std::string& suffix) -> JPNozzleConfig& {
        std::string id;
        if (!nozzles.empty()) {
            id = take(nozzles);
        } else {
            JPNozzleConfig n;
            n.id = id = JPSetupEdits::newId(cell, "NOZ");
            n.mount.headId = headId;
            cell.nozzles.push_back(n);
        }
        JPNozzleConfig& n = *std::find_if(cell.nozzles.begin(), cell.nozzles.end(), [&id](const JPNozzleConfig& x) { return x.id == id; });
        n.name = "N" + suffix;
        n.mount.axisX = axisX;
        n.mount.axisY = axisY;
        return n;
    };
    auto freeLetter = [&cell, &driverId](JPAxisConfig::Type type) {
        // The next letter the controller has free: Z for a Z, else after it.
        std::set<std::string> used;
        for (const JPAxisConfig& a : cell.axes)
            if (a.driverId == driverId) used.insert(a.letter);
        for (const char* l : type == JPAxisConfig::Type::Z ? std::initializer_list<const char*> { "Z", "A", "B", "C", "U", "V", "W" }
                                                           : std::initializer_list<const char*> { "A", "B", "C", "U", "V", "W" })
            if (!used.count(l)) return std::string(l);
        return std::string();
    };
    auto axis = [&](std::deque<std::string>& reuse, JPAxisConfig::Kind k, JPAxisConfig::Type type, const std::string& suffix) -> std::string {
        std::string id;
        if (!reuse.empty()) {
            id = take(reuse);
        } else {
            JPAxisConfig a;
            a.id = id = JPSetupEdits::newId(cell, "AXS");
            a.kind = k;
            a.type = type;
            if (k == JPAxisConfig::Kind::Controller) {
                a.driverId = driverId;
                a.letter = freeLetter(type);
                if (type == JPAxisConfig::Type::Rotation) {
                    a.limitRotation = true;
                    a.softLimitLow = -kRotationLimitDeg;
                    a.softLimitHigh = kRotationLimitDeg;
                    a.feedratePerSecond = kRotationFeedPerS;
                    a.accelerationPerSecond2 = kRotationAccelerationFactor * kRotationFeedPerS;
                }
            }
            // Among the axes of its type, as OpenPnP keeps them ordered by type.
            const auto at = std::find_if(cell.axes.begin(), cell.axes.end(), [type](const JPAxisConfig& x) { return x.type > type; });
            cell.axes.insert(at, a);
        }
        JPAxisConfig* a = axisOf(cell, id);
        // OpenPnP's Type.getDefaultLetter: the type's letter, C for a rotation.
        static const char* const kLetters[] = { "X", "Y", "Z", "C" };
        a->name = std::string(kLetters[int(type)]) + suffix;
        return id;
    };
    auto actuator = [&](std::deque<std::string>& reuse, const std::string& suffix) -> std::string {
        std::string id;
        if (!reuse.empty()) {
            id = take(reuse);
        } else {
            JPActuatorConfig a;
            a.id = id = JPSetupEdits::newId(cell, "ACT");
            a.driverId = driverId;
            a.mount.headId = headId;
            cell.actuators.push_back(a);
        }
        for (JPActuatorConfig& a : cell.actuators)
            if (a.id == id) a.name = "A" + suffix;
        return id;
    };
    auto vacuum = [&](const std::string& nozzleId, const std::string& suffix) {
        const std::string valve = actuator(valves, "VAC" + suffix), sense = actuator(senses, "VACS" + suffix);
        for (JPNozzleConfig& n : cell.nozzles)
            if (n.id == nozzleId) {
                n.vacuumActuatorId = valve;
                n.vacuumSenseActuatorId = sense;
            }
        // A valve made apart from a shared sense actuator: its switching taken from it (OpenPnP's
        // assignVacuumActuators, so the command is not lost), and the output its command names with it ({index}):
        // without it the valve sent "M64 P", which grblHAL refuses (OpenPnP's new actuator, index 0, switches P0).
        JPActuatorConfig* v = nullptr;
        const JPActuatorConfig* s = nullptr;
        for (JPActuatorConfig& a : cell.actuators) {
            if (a.id == valve) v = &a;
            if (a.id == sense) s = &a;
        }
        if (v && s && v->onCommand.empty() && !s->onCommand.empty()) {
            v->onCommand = s->onCommand;
            v->offCommand = s->offCommand;
            if (v->index.empty()) v->index = s->index;
        }
    };
    using K = JPAxisConfig::Kind;
    using T = JPAxisConfig::Type;
    for (int i = 0; i < units; ++i) {
        const std::string suffix = units > 1 ? std::to_string(i + 1) : std::string();
        const std::string suffix1 = std::to_string(i * 2 + 1), suffix2 = std::to_string(i * 2 + 2);
        if (kind == Kind::Standalone) {
            const std::string n1 = nozzle(suffix).id;
            const std::string z = axis(axesZ, K::Controller, T::Z, suffix), c = axis(axesC, K::Controller, T::Rotation, suffix);
            for (JPNozzleConfig& n : cell.nozzles)
                if (n.id == n1) {
                    n.mount.axisZ = z;
                    n.mount.axisRotation = c;
                }
            vacuum(n1, suffix);
            continue;
        }
        const std::string n1 = nozzle(suffix1).id, n2 = nozzle(suffix2).id;
        std::string z1, z2;
        const std::string c1 = axis(axesC, K::Controller, T::Rotation, suffix1);
        if (kind == Kind::DualNegated) {
            z1 = axis(axesZ, K::Controller, T::Z, suffix1);
            z2 = axis(negated, K::Mapped, T::Z, suffix2);
            JPAxisConfig* m = axisOf(cell, z2);
            m->kind = K::Mapped;
            m->inputAxisId = z1;
            m->mapInput0 = 0;
            m->mapOutput0 = 0;
            m->mapInput1 = 1;
            m->mapOutput1 = -1;
        } else {
            // A cam pair on one motor: both cams on it, the second turning the other way.
            const std::string motor = axis(axesZ, K::Controller, T::Z, "N" + suffix);
            z1 = axis(camsCcw, K::Cam, T::Z, suffix1);
            z2 = axis(camsCw, K::Cam, T::Z, suffix2);
            for (const auto& [id, clockwise] : { std::pair { z1, false }, std::pair { z2, true } }) {
                JPAxisConfig* a = axisOf(cell, id);
                a->kind = K::Cam;
                a->inputAxisId = motor;
                a->camClockwise = clockwise;
            }
        }
        const std::string c2 = axis(axesC, K::Controller, T::Rotation, suffix2);
        for (JPNozzleConfig& n : cell.nozzles) {
            if (n.id == n1) {
                n.mount.axisZ = z1;
                n.mount.axisRotation = c1;
            }
            if (n.id == n2) {
                n.mount.axisZ = z2;
                n.mount.axisRotation = c2;
            }
        }
        vacuum(n1, suffix1);
        vacuum(n2, suffix2);
    }
    // What is left over goes.
    std::set<std::string> unusedAxes, unusedActuators;
    for (const auto* list : { &negated, &camsCcw, &camsCw, &axesZ, &axesC })
        for (const std::string& id : *list) unusedAxes.insert(id);
    for (const auto* list : { &valves, &senses })
        for (const std::string& id : *list) unusedActuators.insert(id);
    std::erase_if(cell.axes, [&unusedAxes](const JPAxisConfig& a) { return unusedAxes.count(a.id) > 0; });
    std::erase_if(cell.actuators, [&unusedActuators](const JPActuatorConfig& a) { return unusedActuators.count(a.id) > 0; });
    std::erase_if(cell.nozzles, [&nozzles](const JPNozzleConfig& n) { return std::find(nozzles.begin(), nozzles.end(), n.id) != nozzles.end(); });
}

} // inline namespace jf
