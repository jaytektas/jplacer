// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// An OpenPnP machine.xml becomes a cell: the serial controller with its flow
// control, controller / virtual / mapped axes, the nozzle and its vacuum
// actuator, cameras on the head and the machine, OpenPnP command templates
// rewritten, and what could not be carried over said in the notes.
// Tests check with assert(); a Release build must not compile it away.
#undef NDEBUG
#include <cassert>

#include "openpnp/JPOpenPnpMachineImporter.h"

#include <algorithm>
#include <cmath>
#include <string>

using namespace jf;

static bool noted(const std::vector<std::string>& notes, const std::string& part) {
    return std::any_of(notes.begin(), notes.end(), [&](const std::string& n) { return n.find(part) != std::string::npos; });
}

int main() {
    JPCellConfig cell;
    std::vector<std::string> notes;
    std::string error;
    const bool ok = JPOpenPnpMachineImporter::import(std::string(JPLACER_TESTDATA_DIR) + "/openpnp-machine.xml",
                                                     cell, notes, error);
    assert(ok && error.empty());

    assert(cell.drivers.size() == 1);
    const JPDriverConfig& d = cell.drivers[0];
    assert(d.name == "Jaytek" && d.link["type"].str() == "serial");
    assert(d.link["port"].str() == "/dev/ttyACM0" && d.link["baud"].number() == 115200);
    assert(d.link["flowControl"].str() == "rtscts");
    assert(d.commandTimeoutMs == 30000 && d.homeTimeoutMs == 60000 && d.connectWaitMs == 3000);
    // OpenPnP's home command, every line in order, comments and comment-only lines gone.
    assert(d.commands.at("home") == "M18 Z\nG4 P1\nM17 Z\n$HY\n$HX\nG92 X390 Y444 A0 B0 C0\n$HZ\nG92 Z-25.5\nM400");
    assert(noted(notes, "fiducial"));                       // visual homing is not carried over

    assert(cell.axes.size() == 4);                         // the cam axis is left out
    assert(noted(notes, "axis cam"));
    const JPAxisConfig* x = cell.axis("AX");
    assert(x && x->kind == JPAxisConfig::Kind::Controller && x->letter == "X" && x->driverId == "DRV1");
    assert(x->homeCoordinate == 390 && x->softLimitHigh == 390 && x->softLimitHighEnabled);
    assert(x->feedratePerSecond == 750 && x->accelerationPerSecond2 == 4000);
    // OpenPnP's backlash (sneak-up here) becomes one-sided positioning, measured offset and speed kept.
    assert(x->backlash == JPAxisConfig::Backlash::OneSided && std::abs(x->backlashOffset - 0.024435) < 1e-6);
    assert(x->backlashSpeedFactor == 0.25);
    assert(cell.axis("AZT")->kind == JPAxisConfig::Kind::Virtual);
    const JPAxisConfig* zr = cell.axis("AZR");
    assert(zr->kind == JPAxisConfig::Kind::Mapped && zr->inputAxisId == "AZ");
    assert(zr->mapped(-2) == 2.0 && zr->unmapped(3) == -3.0);   // the inverted side of a shared Z

    assert(cell.heads.size() == 1 && cell.heads[0].id == "H1");
    const JPHeadConfig& head = cell.heads[0];
    assert(head.visualHoming && head.homingFiducial && head.homingFiducial->x == 137.137 && head.homingFiducial->y == 179.265);
    assert(head.park && head.park->x == 390 && head.park->y == 420);
    assert(head.homingFiducialDiameter == 1.85);   // the rig's primary fiducial is the homing mark
    assert(head.rigPrimary && head.rigPrimary->z == -23.6 && head.rigPrimaryDiameter == 1.85);
    assert(head.rigSecondary && head.rigSecondary->x == 167.193 && head.rigSecondary->z == -12.7);
    assert(head.pumpActuatorId == "ACT1" && head.pumpControl == "KeepRunning" && head.pumpOnWaitMs == 60000);
    assert(cell.nozzles.size() == 1);
    const JPNozzleConfig& n = cell.nozzles[0];
    assert(n.mount.headId == "H1" && n.mount.axisZ == "AZR" && n.mount.offsetX == 22.458);
    assert(n.vacuumActuatorId == "ACT1");

    assert(cell.actuators.size() == 2);
    const JPActuatorConfig& sol = cell.actuators[0];
    assert(sol.name == "RIGHT_SOLENOID" && sol.index == "4");
    assert(sol.onCommand == "M64 P{index}" && sol.offCommand == "M65 P{index}");
    assert(sol.readCommand == "M1000 P0" && sol.readPattern == "-?(\\d+)");
    const JPActuatorConfig& photon = cell.actuators[1];
    assert(photon.mount.headId.empty() && photon.driverId == "DRV1");   // found by its commands
    assert(photon.onCommand == "M64 P{index}");            // the driver's default switch command
    assert(photon.readCommand == "M485 {value} {Foo:%d}");
    assert(noted(notes, "{Foo}"));

    assert(cell.cameras.size() == 2);
    assert(!cell.cameras[0].looksUp && cell.cameras[0].mount.headId == "H1");
    assert(noted(notes, "camera TOP_CAMERA: OpenPnP's camera calibration is not imported"));
    assert(cell.cameras[0].device["unique-id"].str() == "top: usb-1");
    assert(cell.cameras[0].device["backend"].str() == "v4l2" && cell.cameras[0].device["name"].str() == "top:");
    assert(cell.cameras[0].device["fps"].number() == 5.0);
    assert(cell.cameras[1].looksUp && cell.cameras[1].mount.headId.empty() && cell.cameras[1].mount.offsetZ == -24);

    // Nothing in the imported cell points at nothing.
    assert(cell.problems().empty());

    // The cell survives a round trip through its own file format.
    JPCellConfig again;
    assert(again.fromJson(cell.toJson(), error));
    assert(again.toJson().dump() == cell.toJson().dump());

    JPCellConfig none;
    assert(!JPOpenPnpMachineImporter::import(std::string(JPLACER_TESTDATA_DIR) + "/missing.xml", none, notes, error));
    return 0;
}
