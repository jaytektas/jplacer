// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// An OpenPnP machine.xml becomes a cell: the serial controller with its flow
// control, controller / virtual / mapped axes, the nozzle, its vacuum
// actuator and nozzle tips, cameras on the head and the machine, OpenPnP command templates
// rewritten, and what could not be carried over said in the notes.
// Tests check with assert(); a Release build must not compile it away.
#undef NDEBUG
#include <cassert>

#include "openpnp/JPOpenPnpMachineImporter.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>
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
    // The machine's own settings.
    // Home after connected: a controller's. A cell kept with it on the machine
    // (before) gives it to every controller, and keeps it there from then on.
    {
        JJson j = cell.toJson();
        j["homeAfterConnect"] = true;
        JPCellConfig was;
        std::string e;
        assert(was.fromJson(j, e) && was.homeAfterConnect());
        for (const JPDriverConfig& dc : was.drivers) assert(dc.homeAfterConnect);
        assert(!was.toJson()["homeAfterConnect"].boolean() && was.toJson()["drivers"][0]["homeAfterConnect"].boolean());
    }
    assert(!cell.homeAfterConnect() && cell.parkAfterHome && cell.discardLocation && cell.discardLocation->x == 40.935);
    // Auto tool select (on unless said off), auto-load most recent job, the default board location.
    assert(cell.autoToolSelect && cell.autoLoadMostRecentJob);
    assert(cell.defaultBoardLocation.x == 120.0 && cell.defaultBoardLocation.y == 80.5 && cell.defaultBoardLocation.rotation == 90.0);
    {
        std::string e;
        JPCellConfig back;
        JPCellConfig off = cell;
        off.autoToolSelect = false;
        assert(back.fromJson(off.toJson(), e) && !back.autoToolSelect && back.autoLoadMostRecentJob);
        assert(back.defaultBoardLocation.z == -2.0);
    }
    // The rest of OpenPnP's serial settings, in jplacer's words.
    assert(d.link["dataBits"].number() == 8 && d.link["stopBits"].number() == 1 && d.link["parity"].str() == "none");
    assert(!d.link["setDtr"].boolean() && !d.link["setRts"].boolean() && d.link["lineEnding"].str() == "LF");
    assert(d.commandTimeoutMs == 30000 && d.homeTimeoutMs == 60000 && d.connectWaitMs == 3000);
    // OpenPnP's home command, every line in order, comments and comment-only lines gone.
    assert(d.commands.at("home") == "M18 Z\nG4 P1\nM17 Z\n$HY\n$HX\nG92 X390 Y444 A0 B0 C0\n$HZ\nG92 Z-25.5\nM400");
    assert(noted(notes, "fiducial"));                       // visual homing waits for a calibrated camera

    assert(cell.axes.size() == 7);
    // OpenPnP's cam axes: the counter-clockwise one on its rotation axis, the
    // clockwise one its partner's cam turned the other way.
    {
        assert(cell.axis("ACR")->preMoveCommand == "T1 ; B {Coordinate}");
        assert(cell.drivers[0].usingLetterVariables && !cell.drivers[0].supportingPreMove);
        const JPAxisConfig* ccw = cell.axis("ACAM");
        const JPAxisConfig* cw = cell.axis("ACAMCW");
        assert(ccw && ccw->kind == JPAxisConfig::Kind::Cam && ccw->inputAxisId == "ACR" && !ccw->camClockwise);
        assert(ccw->camRadius == 12 && ccw->camArmsAngle == 150);
        assert(cw && cw->kind == JPAxisConfig::Kind::Cam && cw->inputAxisId == "ACR" && cw->camClockwise && cw->camRadius == 12);
        // At the cam's balance (0 degrees with the arms 150 apart: 15 degrees short of level), Z is 12 sin 15 each.
        const double z0 = 12 * std::sin(15 * 3.14159265358979323846 / 180);
        assert(std::abs(*ccw->mapped(0) - z0) < 1e-9 && std::abs(*cw->mapped(0) - z0) < 1e-9);
        // Turned 30 degrees, one goes up as the other goes down, and back again.
        assert(*ccw->mapped(30) > z0 && *cw->mapped(30) < z0);
        assert(std::abs(*ccw->unmapped(*ccw->mapped(30)) - 30) < 1e-9 && std::abs(*cw->unmapped(*cw->mapped(30)) - 30) < 1e-9);
        // Kept to its useful range (180 - 150 / 2 = 105 degrees either way).
        assert(std::abs(*ccw->unmapped(1000) - 105) < 1e-9);
        assert(JPAxisConfig::fromJson(cw->toJson(), error)->camClockwise);
    }
    const JPAxisConfig* x = cell.axis("AX");
    assert(x && x->kind == JPAxisConfig::Kind::Controller && x->letter == "X" && x->driverId == "DRV1");
    assert(x->homeCoordinate == 390 && x->softLimitHigh == 390 && x->softLimitHighEnabled);
    assert(x->feedratePerSecond == 750 && x->accelerationPerSecond2 == 4000);
    // OpenPnP's backlash as it is (sneak-up here), its measured offset, sneak-up and speed kept.
    assert(x->backlash == JPAxisConfig::Backlash::DirectionalSneakUp && std::abs(x->backlashOffset - 0.024435) < 1e-6);
    assert(x->backlashSpeedFactor == 0.25 && std::abs(x->sneakUpMm - 0.056256) < 1e-6);
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
    // The nozzle tips, and which fit the nozzle (a tip OpenPnP no longer has left out).
    assert(cell.nozzleTips.size() == 2 && cell.nozzleTips[0].name == "503R - 0805 / 0603");
    assert(cell.nozzleTips[0].diameter == 0.75 && std::abs(cell.nozzleTips[1].diameter - 3.5) < 1e-6);
    // How its runout is measured comes across (what was measured does not).
    const auto& rc = cell.nozzleTips[0].runoutCalibration;
    assert(rc.enabled && rc.divisions == 8 && rc.misdetects == 1 && std::abs(rc.zOffset - 0.5) < 1e-9);
    assert(cell.nozzleTips[0].runout.empty());
    assert((n.tipIds == std::vector<std::string>{ "TIP1", "TIP2" }) && n.tipId == "TIP2");
    // OpenPnP's changer places become load steps (an unset one left out), its actuator by id;
    // unloading is loading backwards.
    const std::vector<JPChangerStep>& load = cell.nozzleTips[0].loadSteps;
    assert(load.size() == 4 && load[0].x == 410.278 && load[0].z == 0.0 && load[0].speed == 1);
    assert(load[1].z == -27.0 && load[1].speed == 0.5);
    assert(load[2].kind == JPChangerStep::Kind::Actuator && load[2].actuatorId == "ACT1" && load[2].on);
    assert(load[3].x == 390.278 && load[3].speed == 0.25);
    assert(cell.nozzleTips[0].unloadReversesLoad && cell.nozzleTips[1].loadSteps.empty());

    assert(cell.actuators.size() == 3);
    // A profile actuator: its actuators, and its profiles' defaults and values.
    {
        const JPActuatorConfig& lights = cell.actuators[2];
        assert(lights.valueType == JPActuatorConfig::ValueType::Profile && lights.profiles.size() == 2);
        assert(lights.profileActuators[0] == "ACT1" && lights.profileActuators[1] == "ACT2" && lights.profileActuators[2].empty());
        assert(lights.defaultProfile(false)->name == "Off" && lights.defaultProfile(true)->name == "All");
        assert(lights.profiles[1].values[1] == "true" && lights.profiles[0].values[1].empty());
        assert(lights.canSwitch() && lights.canSet());
        const JPActuatorConfig back = JPActuatorConfig::fromJson(lights.toJson());
        assert(back.valueType == JPActuatorConfig::ValueType::Profile && back.profileNamed("All")->values[0] == "true");
    }
    // Signalers: OpenPnP's sound and actuator ones; any other is left out and said.
    assert(cell.signalers.size() == 2);
    assert(cell.signalers[0].kind == JPSignalerConfig::Kind::Sound && cell.signalers[0].id == "SIG1");
    assert(cell.signalers[0].errorSound && !cell.signalers[0].finishedSound);
    assert(cell.signalers[1].kind == JPSignalerConfig::Kind::Actuator && cell.signalers[1].name == "Beacon");
    assert(cell.signalers[1].actuatorId == "ACT1" && cell.signalers[1].jobState == JPSignalerConfig::JobState::Error);
    {
        bool said = false;
        for (const std::string& n : notes) said |= n.find("Neoden4Signaler") != std::string::npos;
        assert(said);
        std::string e;
        JPCellConfig back;
        assert(back.fromJson(cell.toJson(), e));
        assert(back.signalers.size() == 2 && back.signalers[1].jobState == JPSignalerConfig::JobState::Error);
        assert(!back.signalers[0].jobState && back.signalers[0].errorSound);
    }
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
    // Its light, settling and white balance, as OpenPnP had them.
    assert(cell.cameras[0].light.beforeCapture && cell.cameras[0].light.antiGlare && !cell.cameras[0].light.afterCapture);
    assert(cell.cameras[0].settle.method == "Euclidean" && cell.cameras[0].settle.threshold == 0.45 && cell.cameras[0].settle.debounce == 5);
    assert(cell.cameras[0].whiteBalance.balance[2] == 1.375 && cell.cameras[0].whiteBalance.gamma[2] == 1.09);
    assert(cell.cameras[1].looksUp && cell.cameras[1].mount.headId.empty() && cell.cameras[1].mount.offsetZ == -24);
    // The image transforms its advanced calibration still applies: de-interlace and crop.
    assert(cell.cameras[1].deinterlace && cell.cameras[1].cropWidth == 400 && cell.cameras[1].cropHeight == 0);
    assert(!cell.cameras[0].deinterlace && cell.cameras[0].cropWidth == 0);
    // OpenPnP's preview rate is its fps (5 unless set); suspend and auto view off unless set.
    assert(cell.cameras[1].previewFps == 5 && !cell.cameras[1].suspendDuringTasks && !cell.cameras[1].autoCameraView);
    assert(JPCameraConfig::fromJson(cell.cameras[1].toJson()).cropWidth == 400);

    // Nothing in the imported cell points at nothing.
    assert(cell.problems().empty());

    // The cell survives a round trip through its own file format.
    JPCellConfig again;
    assert(again.fromJson(cell.toJson(), error));
    assert(again.toJson().dump() == cell.toJson().dump());

    // Imported again over a cell used here: what was set here is kept.
    {
        JPCellConfig used = cell;
        used.drivers[0].link["port"] = "/dev/serial/by-id/the-controller";
        for (JPNozzleConfig& n : used.nozzles) n.tipId.clear();   // taken off by hand
        used.cameras[0].showAll = 0.7;
        used.nozzleTips[0].loadSteps.clear();
        used.nozzleTips[0].unloadReversesLoad = false;
        JPCellConfig fresh = cell;
        assert(!fresh.nozzles[0].tipId.empty() || !fresh.nozzles.back().tipId.empty());   // OpenPnP believes one is on
        JPOpenPnpMachineImporter::keepFrom(used, fresh);
        assert(fresh.drivers[0].link["port"].str() == "/dev/serial/by-id/the-controller");
        assert(fresh.drivers[0].link["flowControl"].str() == "rtscts");   // OpenPnP's other settings come through
        for (const JPNozzleConfig& n : fresh.nozzles) assert(n.tipId.empty());
        assert(fresh.cameras[0].showAll == 0.7 && fresh.nozzleTips[0].loadSteps.empty() && !fresh.nozzleTips[0].unloadReversesLoad);
    }

    JPCellConfig none;
    assert(!JPOpenPnpMachineImporter::import(std::string(JPLACER_TESTDATA_DIR) + "/missing.xml", none, notes, error));
    // The top camera's own settings as OpenPnP set them; one OpenPnP left alone is left out.
    {
        const JJson& controls = cell.cameras.front().device["controls"];
        assert(!controls["exposure"]["auto"].boolean() && controls["exposure"]["value"].number() == 1432);
        assert(controls["white-balance"]["auto"].boolean() && !controls["white-balance"]["value"].isNumber());
        assert(!controls["focus"].isObject());
    }

    // OpenPnP's non-squareness, a linear transform X axis (X + factorY Y +
    // offset) that the top camera rides on: jplacer's squareness, pivoted
    // where the offset makes it zero, and the camera on the input X axis.
    {
        std::ifstream in(std::string(JPLACER_TESTDATA_DIR) + "/openpnp-machine.xml");
        std::stringstream ss;
        ss << in.rdbuf();
        std::string xml = ss.str();
        const size_t at = xml.find('>', xml.find("<axes"));
        assert(at != std::string::npos);
        xml.insert(at + 1,
                   R"(<axis class="org.openpnp.machine.reference.axis.ReferenceControllerAxis" id="AY" name="y" type="Y" letter="Y" driver-id="DRV1"/>)"
                   R"(<axis class="org.openpnp.machine.reference.axis.ReferenceLinearTransformAxis" id="AXSQ" name="x" type="X" )"
                   R"(input-axis-x-id="AX" input-axis-y-id="AY" factor-x="1.0" factor-y="-0.0032">)"
                   R"(<offset value="0.5736" units="Millimeters"/></axis>)");
        const std::string camAxis = R"(name="TOP_CAMERA" looking="Down" axis-X-id="AX")";
        const size_t cam = xml.find(camAxis);
        assert(cam != std::string::npos);
        xml.replace(cam, camAxis.size(), R"(name="TOP_CAMERA" looking="Down" axis-X-id="AXSQ")");
        const std::string path = (std::filesystem::temp_directory_path() / "jplacer-test-nonsquare.xml").string();
        std::ofstream(path) << xml;
        JPCellConfig sq;
        std::vector<std::string> sqNotes;
        assert(JPOpenPnpMachineImporter::import(path, sq, sqNotes, error));
        std::filesystem::remove(path);
        assert(sq.squareness.axisX == "AX" && sq.squareness.axisY == "AY");
        assert(std::abs(sq.squareness.xPerY + 0.0032) < 1e-12 && std::abs(sq.squareness.atY - 179.25) < 1e-9);
        assert(!sq.axis("AXSQ") && sq.cameras.front().mount.axisX == "AX");
        assert(sq.problems().empty());
    }

    return 0;
}
