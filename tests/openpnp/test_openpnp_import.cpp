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
    assert(head.zProbeActuatorId == "ACT1");   // its Z probe, by name
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
    // A tip's changer slot vision calibration and part detection: its settings, and its template
    // pictures as OpenPnP's files beside machine.xml; saved and read back the same.
    {
        std::ifstream in(std::string(JPLACER_TESTDATA_DIR) + "/openpnp-machine.xml");
        std::stringstream ss;
        ss << in.rdbuf();
        std::string xml = ss.str();
        const std::string tip = R"(id="TIP1" name="503R - 0805 / 0603">)";
        const size_t at = xml.find(tip);
        assert(at != std::string::npos);
        xml.replace(at, tip.size(),
                    R"(id="TIP1" name="503R - 0805 / 0603" vision-calibration="ThirdLocation" vision-calibration-trigger="NozzleTipChange" )"
                    R"(vision-match-minimum-score="0.35" vision-calibration-max-passes="5" establish-part-on-level="true">)"
                    R"(<part-on-check-align>false</part-on-check-align><part-off-check-before-pick>false</part-off-check-before-pick>)"
                    R"(<vision-calibration-z-adjust value="-1.5" units="Millimeters"/>)"
                    R"(<vision-template-dimension-x value="8.0" units="Millimeters"/>)"
                    R"(<vision-template-dimension-y value="6.0" units="Millimeters"/>)"
                    R"(<vision-template-tolerance value="3.0" units="Millimeters"/>)"
                    R"(<vision-calibration-tolerance value="0.4" units="Millimeters"/>)"
                    R"(<vision-template-image-empty hash="abc123"/><vision-template-image-occupied hash="def456"/>)");
        const std::filesystem::path dir = std::filesystem::temp_directory_path() / "jplacer-test-slot";
        std::filesystem::create_directories(dir);
        const std::string path = (dir / "machine.xml").string();
        std::ofstream(path) << xml;
        JPCellConfig vc;
        std::vector<std::string> vcNotes;
        assert(JPOpenPnpMachineImporter::import(path, vc, vcNotes, error));
        std::filesystem::remove_all(dir);
        const JPNozzleTipConfig::VisionCalibration& v = vc.nozzleTips[0].visionCalibration;
        assert(v.location == "ThirdLocation" && v.trigger == "NozzleTipChange" && v.minimumScore == 0.35 && v.maxPasses == 5);
        assert(v.zAdjustMm == -1.5 && v.templateWidthMm == 8 && v.templateHeightMm == 6 && v.toleranceMm == 3 && v.precisionMm == 0.4);
        assert(v.templateEmpty == (dir / "org.openpnp.vision.TemplateImage" / "abc123.png").string());
        assert(v.templateOccupied == (dir / "org.openpnp.vision.TemplateImage" / "def456.png").string());
        // Its place: the tip's Third Location, Z adjusted.
        const auto place = vc.nozzleTips[0].visionCalibrationPlace();
        const auto changer = vc.nozzleTips[0].openPnpChanger();
        assert(place && changer && changer->at[2] && place->x == changer->at[2]->x && place->z == changer->at[2]->z - 1.5);
        const JPNozzleTipConfig back = JPNozzleTipConfig::fromJson(vc.nozzleTips[0].toJson());
        assert(back.visionCalibration.location == v.location && back.visionCalibration.templateOccupied == v.templateOccupied
               && back.visionCalibration.precisionMm == 0.4 && back.visionCalibration.maxPasses == 5);
        // Its part detection's Establish Level? and Perform Checks?.
        const JPNozzleTipConfig& pd = vc.nozzleTips[0];
        assert(pd.partOn.establish && !pd.partOff.establish && pd.partOnCheckAfterPick && !pd.partOnCheckAlign
               && pd.partOnCheckBeforePlace && pd.partOffCheckAfterPlace && !pd.partOffCheckBeforePick);
        const JPNozzleTipConfig pdBack = JPNozzleTipConfig::fromJson(pd.toJson());
        assert(pdBack.partOn.establish && !pdBack.partOnCheckAlign && !pdBack.partOffCheckBeforePick && pdBack.partOffCheckAfterPlace);
        // The other tip: none.
        assert(!vc.nozzleTips[1].visionCalibration.on() && !vc.nozzleTips[1].visionCalibrationPlace());
    }
    // OpenPnP's OpenCvCamera (a device index, OpenCV's properties) and Webcam (by name): capture devices.
    {
        std::ifstream in(std::string(JPLACER_TESTDATA_DIR) + "/openpnp-machine.xml");
        std::stringstream ss;
        ss << in.rdbuf();
        std::string xml = ss.str();
        const size_t at = xml.find('>', xml.find(R"(<cameras class="java.util.ArrayList">)", xml.find("BOTTOM_CAMERA") - 200));
        assert(at != std::string::npos);
        xml.insert(at + 1,
                   R"(<camera class="org.openpnp.machine.reference.camera.OpenCvCamera" id="CV" name="CV_CAMERA" looking="Up" deviceIndex="2" preferred-width="1280" preferred-height="720">)"
                   R"(<properties><open-cv-capture-property-value property="CAP_PROP_EXPOSURE" value="157.0" set-before-open="false" set-after-open="true"/>)"
                   R"(<open-cv-capture-property-value property="CAP_PROP_AUTOFOCUS" value="0.0" set-before-open="false" set-after-open="true"/>)"
                   R"(<open-cv-capture-property-value property="CAP_PROP_GUID" value="1.0" set-before-open="false" set-after-open="true"/></properties></camera>)"
                   R"(<camera class="org.openpnp.machine.reference.camera.Webcams" id="WC" name="WEB_CAMERA" looking="Up" device-id="HD Webcam C525 /dev/video4" preferred-width="640" preferred-height="480"/>)"
                   R"(<camera class="org.openpnp.machine.reference.camera.SimulatedUpCamera" id="SU" name="SIM_UP" looking="Up" width="800" height="600" simulated-flipped="true">)"
                   R"(<simulated-units-per-pixel units="Millimeters" x="0.02" y="0.02" z="0.0" rotation="0.0"/></camera>)");
        const std::string path = (std::filesystem::temp_directory_path() / "jplacer-test-cameras.xml").string();
        std::ofstream(path) << xml;
        JPCellConfig cams;
        std::vector<std::string> camNotes;
        assert(JPOpenPnpMachineImporter::import(path, cams, camNotes, error));
        std::filesystem::remove(path);
        const JPCameraConfig* cv = nullptr;
        const JPCameraConfig* web = nullptr;
        for (const JPCameraConfig& c : cams.cameras) {
            if (c.id == "CV") cv = &c;
            if (c.id == "WC") web = &c;
        }
        assert(cv && web);
        assert(cv->device["backend"].str() == "v4l2" && cv->device["name"].str() == "/dev/video2" && cv->device["width"].number() == 1280);
        assert(!cv->device["controls"]["exposure"]["auto"].boolean() && cv->device["controls"]["exposure"]["value"].number() == 157);
        assert(!cv->device["controls"]["focus"]["auto"].boolean());
        bool guid = false;
        for (const std::string& n : camNotes) guid = guid || n.find("CAP_PROP_GUID") != std::string::npos;
        assert(guid);
        assert(web->device["name"].str() == "HD Webcam C525" && web->device["height"].number() == 480);
        const JPCameraConfig* su = nullptr;
        for (const JPCameraConfig& c : cams.cameras) if (c.id == "SU") su = &c;
        assert(su && su->device["backend"].str() == "simulated" && su->device["width"].number() == 800);
        const JJson& m = su->device["scene"]["pxPerMm"];
        assert(std::abs(m[size_t(0)].number() + 50) < 1e-9 && std::abs(m[size_t(3)].number() - 50) < 1e-9);   // flipped, 50 px/mm
    }
    // OpenPnP's SimulationModeMachine: its simulated imperfections.
    {
        std::ifstream in(std::string(JPLACER_TESTDATA_DIR) + "/openpnp-machine.xml");
        std::stringstream ss;
        ss << in.rdbuf();
        std::string xml = ss.str();
        const std::string reference = R"(<machine class="org.openpnp.machine.reference.ReferenceMachine">)";
        const size_t at = xml.find(reference);
        assert(at != std::string::npos);
        xml.replace(at, reference.size(),
                    R"(<machine class="org.openpnp.machine.reference.SimulationModeMachine" simulation-mode="DynamicImperfectionsMachine" )"
                    R"(replacing-drivers="false" simulated-non-squareness-factor="0.002" simulated-runout-phase="45.0" )"
                    R"(simulated-camera-noise="20" simulated-camera-lag="0.05" simulated-vibration-amplitude="0.1" )"
                    R"(simulated-vibration-duration="0.3">)"
                    R"(<simulated-runout value="0.05" units="Millimeters"/>)"
                    R"(<homing-error units="Millimeters" x="0.4" y="-0.2" z="0.0" rotation="0.0"/>)");
        const std::string path = (std::filesystem::temp_directory_path() / "jplacer-test-simulation.xml").string();
        std::ofstream(path) << xml;
        JPCellConfig simCell;
        std::vector<std::string> simNotes;
        assert(JPOpenPnpMachineImporter::import(path, simCell, simNotes, error));
        std::filesystem::remove(path);
        const JPSimulationConfig& sim = simCell.simulation;
        assert(sim.dynamic() && !sim.replaceDrivers && sim.nonSquarenessFactor == 0.002 && sim.runoutPhaseDeg == 45);
        assert(sim.cameraNoise == 20 && sim.cameraLagS == 0.05 && sim.vibrationAmplitudeMm == 0.1 && sim.vibrationDurationS == 0.3);
        assert(std::abs(sim.runoutMm - 0.05) < 1e-12 && sim.homingErrorX == 0.4 && sim.homingErrorY == -0.2);
        assert(!cell.simulation.on());   // a ReferenceMachine is not simulated
    }
    // OpenPnP's own default machine: its old single NullDriver migrated as
    // OpenPnP's load does (the driver one of the machine's, X and Y for all, a
    // Z and rotation of its own for the nozzle, virtual ones for the camera,
    // at the old feed rate), the NullDriver a simulated controller with
    // letters, and the ImageCamera's picture (none shipped with the test: a note).
    {
        JPCellConfig def;
        std::vector<std::string> defNotes;
        assert(JPOpenPnpMachineImporter::import(std::string(JPLACER_TESTDATA_DIR) + "/../../openpnp-defaults/config/machine.xml",
                                                def, defNotes, error));
        assert(def.drivers.size() == 1 && def.drivers[0].name == "NullDriver" && def.drivers[0].link["type"].str() == "simulated");
        assert(def.drivers[0].link["simulator"]["axisLetters"].size() == 4);
        auto ax = [&def](const std::string& name) -> const JPAxisConfig& {
            for (const JPAxisConfig& a : def.axes)
                if (a.name == name) return a;
            assert(false);
            return def.axes.front();
        };
        assert(ax("x").letter == "X" && ax("y").letter == "Y" && ax("zN1").letter == "Z" && ax("rotationN1").letter == "A");
        assert(std::abs(ax("x").feedratePerSecond - 20000.0 / 60) < 1e-3 && std::abs(ax("x").accelerationPerSecond2 - 40000.0 / 60) < 1e-3);
        assert(std::abs(ax("rotationN1").feedratePerSecond - 200000.0 / 60) < 1e-2 && ax("rotationN1").limitRotation);
        assert(ax("zN1").safeZoneLowEnabled && ax("zN1").safeZoneHighEnabled && ax("zN1").safeZoneLow == 0);
        assert(ax("zTop").kind == JPAxisConfig::Kind::Virtual && ax("rotationTop").kind == JPAxisConfig::Kind::Virtual);
        assert(def.nozzles.size() == 1 && def.nozzles[0].mount.axisZ == ax("zN1").id && def.nozzles[0].mount.axisRotation == ax("rotationN1").id);
        assert(def.heads.size() == 1 && def.heads[0].homingFiducial && def.heads[0].homingFiducial->x == 5.736);
        bool imageCamera = false;
        for (const JPCameraConfig& cam : def.cameras)
            if (cam.device["backend"].str() == "image") {
                imageCamera = true;
                // Its picture's scale the camera's own, as OpenPnP's getImageUnitsPerPixel.
                assert(cam.device["imageUnitsPerPixel"]["x"].number() == 0.04233);
            }
        assert(imageCamera);
        // Actuators naming no controller are the first one's, as OpenPnP's getDriver falls back.
        for (const JPActuatorConfig& a : def.actuators) assert(a.driverId == def.drivers[0].id);
        for (const std::string& n : defNotes) assert(n.find("not a kind") == std::string::npos && n.find("left out") == std::string::npos);
    }

    return 0;
}
