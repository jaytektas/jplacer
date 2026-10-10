// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// OpenPnP's nozzle solution: a head's nozzles made as so many units of a
// kind, the nozzles, axes and actuators there were reused in order (their
// settings kept), new ones made as OpenPnP makes them, and those left over
// removed; the head's current kind and units read back. A nozzle whose valve
// and sensing were one actuator, its command naming its output by {index}: the
// valve made apart switches that same output.
// Tests check with assert(); a Release build must not compile it away.
#undef NDEBUG
#include <cassert>

#include "setup/JPNozzleSolution.h"

#include <string>

using namespace jf;

namespace {

JPCellConfig cellConfig() {
    const std::string json = R"({
      "name": "Solution",
      "drivers": [ { "id": "D", "name": "Gantry", "link": { "type": "simulated", "simulator": {} } } ],
      "heads": [ { "id": "H", "name": "Head" } ],
      "axes": [ { "id": "X", "name": "x", "kind": "controller", "type": "x", "driver": "D", "letter": "X" },
                { "id": "Y", "name": "y", "kind": "controller", "type": "y", "driver": "D", "letter": "Y" },
                { "id": "Z", "name": "z", "kind": "controller", "type": "z", "driver": "D", "letter": "Z", "feedratePerSecond": 77 },
                { "id": "C", "name": "c", "kind": "controller", "type": "rotation", "driver": "D", "letter": "A" } ],
      "cameras": [ { "id": "CAM", "name": "Top", "looking": "down", "mount": { "head": "H", "axisX": "X", "axisY": "Y" },
                     "device": { "backend": "simulated" } } ],
      "actuators": [ { "id": "V", "name": "Vac", "driver": "D", "mount": { "head": "H" }, "onCommand": "M64 P1", "offCommand": "M65 P1" } ],
      "nozzles": [ { "id": "N", "name": "N1", "vacuumActuator": "V",
                     "mount": { "head": "H", "axisX": "X", "axisY": "Y", "axisZ": "Z", "axisRotation": "C" } } ]
    })";
    JPCellConfig c;
    std::string error;
    const bool ok = c.fromJson(JJson::parse(json), error);
    assert(ok && c.problems().empty());
    return c;
}

const JPNozzleConfig* nozzle(const JPCellConfig& c, const std::string& name) {
    for (const JPNozzleConfig& n : c.nozzles)
        if (n.name == name) return &n;
    return nullptr;
}

const JPActuatorConfig* actuator(const JPCellConfig& c, const std::string& id) {
    for (const JPActuatorConfig& a : c.actuators)
        if (a.id == id) return &a;
    return nullptr;
}

} // namespace

int main() {
    using K = JPNozzleSolution::Kind;
    JPCellConfig c = cellConfig();
    K kind;
    int units = 0;
    JPNozzleSolution::current(c, "H", kind, units);
    assert(kind == K::Standalone && units == 1);

    // A negated pair: the nozzle, its Z and C and its valve reused; the second's Z the first's negated.
    JPNozzleSolution::apply(c, "H", "CAM", K::DualNegated, 1);
    const JPNozzleConfig* n1 = nozzle(c, "N1");
    const JPNozzleConfig* n2 = nozzle(c, "N2");
    assert(c.nozzles.size() == 2 && n1 && n2 && n1->id == "N" && n2->mount.axisX == "X" && n2->mount.axisY == "Y");
    assert(n1->mount.axisZ == "Z" && c.axis("Z")->name == "Z1" && c.axis("Z")->feedratePerSecond == 77);
    const JPAxisConfig* z2 = c.axis(n2->mount.axisZ);
    assert(z2 && z2->kind == JPAxisConfig::Kind::Mapped && z2->inputAxisId == "Z" && z2->mapped(1) == -1.0 && z2->name == "Z2");
    const JPAxisConfig* c2 = c.axis(n2->mount.axisRotation);
    assert(c2 && c2->kind == JPAxisConfig::Kind::Controller && c2->limitRotation && c2->softLimitHigh == 180 && !c2->letter.empty());
    assert(n1->vacuumActuatorId == "V" && actuator(c, "V")->name == "AVAC1");
    assert(!n1->vacuumSenseActuatorId.empty() && actuator(c, n1->vacuumSenseActuatorId)->name == "AVACS1");
    assert(actuator(c, n2->vacuumActuatorId)->name == "AVAC2" && actuator(c, n2->vacuumSenseActuatorId)->name == "AVACS2");
    assert(c.problems().empty());
    JPNozzleSolution::current(c, "H", kind, units);
    assert(kind == K::DualNegated && units == 1);

    // Two cam pairs: four nozzles, each pair's cams on one motor.
    JPNozzleSolution::apply(c, "H", "CAM", K::DualCam, 2);
    assert(c.nozzles.size() == 4 && nozzle(c, "N4"));
    const JPAxisConfig* cam3 = c.axis(nozzle(c, "N3")->mount.axisZ);
    const JPAxisConfig* cam4 = c.axis(nozzle(c, "N4")->mount.axisZ);
    assert(cam3 && cam4 && cam3->kind == JPAxisConfig::Kind::Cam && !cam3->camClockwise && cam4->camClockwise);
    assert(cam3->inputAxisId == cam4->inputAxisId && c.axis(cam3->inputAxisId)->name == "ZN2");
    assert(c.problems().empty());
    JPNozzleSolution::current(c, "H", kind, units);
    assert(kind == K::DualCam && units == 2);

    // Back to one standalone nozzle: the rest removed.
    JPNozzleSolution::apply(c, "H", "CAM", K::Standalone, 1);
    assert(c.nozzles.size() == 1 && c.nozzles.front().id == "N" && c.nozzles.front().name == "N");
    size_t zAxes = 0;
    for (const JPAxisConfig& a : c.axes) zAxes += a.type == JPAxisConfig::Type::Z;
    assert(zAxes == 1 && c.actuators.size() == 2 && c.problems().empty());
    {
        // One actuator the valve and the sensing (as an OpenPnP machine's often is), "M64 P{index}" on output 3.
        JPCellConfig shared = cellConfig();
        JPActuatorConfig& a = shared.actuators.front();
        a.onCommand = "M64 P{index}";
        a.offCommand = "M65 P{index}";
        a.index = "3";
        shared.nozzles.front().vacuumSenseActuatorId = "V";
        JPNozzleSolution::apply(shared, "H", "CAM", K::DualNegated, 1);
        const JPNozzleConfig* n = nozzle(shared, "N1");
        assert(n && n->vacuumSenseActuatorId == "V" && n->vacuumActuatorId != "V");
        const JPActuatorConfig* valve = actuator(shared, n->vacuumActuatorId);
        assert(valve && valve->onCommand == "M64 P{index}" && valve->offCommand == "M65 P{index}");
        assert(valve->index == "3");   // the same output, not "M64 P"
    }
    return 0;
}
