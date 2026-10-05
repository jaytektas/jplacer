// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// Issues & Solutions' checks over a cell: what each milestone finds (a
// simulated controller, a missing or duplicate axis letter, a shared Z,
// no feed rate, the machine not homed, Safe Z and soft limits not set), and
// a Safe Z and a soft limit captured from where the axis is on Accept and
// put back on Reopen.
// Tests check with assert(); a Release build must not compile it away.
#undef NDEBUG
#include <cassert>

#include "setup/JPIssueChecks.h"

#include <map>

using namespace jf;
using S = JPSolutions;

namespace {

JPAxisConfig axis(const std::string& id, JPAxisConfig::Type type, const std::string& letter) {
    JPAxisConfig a;
    a.id = id;
    a.name = id;
    a.type = type;
    a.driverId = "D";
    a.letter = letter;
    a.feedratePerSecond = 100;
    a.accelerationPerSecond2 = 1000;
    return a;
}

const S::Issue* find(const S& s, const std::string& issue) {
    for (const auto& i : s.issues())
        if (i->issue == issue) return i.get();
    return nullptr;
}

} // namespace

int main() {
    JPCellConfig cell;
    JPDriverConfig d;
    d.id = "D";
    d.name = "Gantry";
    d.link = JJson::object();
    d.link["type"] = "simulated";
    cell.drivers.push_back(d);
    cell.axes = { axis("x", JPAxisConfig::Type::X, "X"), axis("y", JPAxisConfig::Type::Y, "X"),
                  axis("z", JPAxisConfig::Type::Z, ""), axis("c", JPAxisConfig::Type::Rotation, "A") };
    cell.axes[3].accelerationPerSecond2 = 0;
    JPHeadConfig h;
    h.id = "H";
    h.name = "Head";
    cell.heads.push_back(h);
    JPNozzleConfig n1, n2;
    n1.id = "N1";
    n1.name = "N1";
    n1.mount = { "H", "x", "y", "z", "c" };
    n2 = n1;
    n2.id = "N2";
    n2.name = "N2";
    cell.nozzles = { n1, n2 };

    bool homed = false;
    std::map<std::string, double> at { { "z", 12.5 }, { "x", -3 } };
    JPIssueChecks::Context c;
    c.cell = [&cell]() -> const JPCellConfig* { return &cell; };
    c.calibrated = [](const std::string&) { return true; };
    c.homed = [&homed] { return homed; };
    c.home = [&homed] { homed = true; };
    c.axisPosition = [&at](const std::string& id) -> std::optional<double> {
        const auto i = at.find(id);
        return i == at.end() ? std::nullopt : std::optional(i->second);
    };
    c.changeCell = [&cell](const std::string&, const std::function<void(JPCellConfig&)>& edit) { edit(cell); };

    S s;
    s.setChecks(JPIssueChecks::all(c));
    // Welcome: nothing but the milestone.
    s.find();
    s.publish();
    assert(s.issues().size() == 1);

    // Connect: the simulated controller.
    s.setTargetMilestone(S::Milestone::Connect);
    s.find();
    s.publish();
    assert(find(s, "Controller not connected to jplacer"));

    // Basics: the letters and the shared axes.
    s.setTargetMilestone(S::Milestone::Basics);
    s.find();
    s.publish();
    assert(find(s, "Axis letter is missing. Assign the letter to continue."));
    assert(find(s, "Duplicate axis letter X on axes x, y."));
    assert(find(s, "Nozzles N1 and N2 have the same Z axis assigned."));
    assert(find(s, "Nozzles N1 and N2 have the same Rotation axis assigned."));
    // The actuators, as OpenPnP's ActuatorSolutions: none assigned; then one with no command, given on Accept.
    assert(find(s, "ReferenceNozzle N1 is missing a vacuum valve actuator."));
    assert(find(s, "ReferenceHead Head is missing a pump control actuator."));
    {
        JPActuatorConfig v;
        v.id = "V";
        v.name = "Vac";
        v.driverId = "D";
        cell.actuators.push_back(v);
        cell.nozzles[0].vacuumActuatorId = "V";
        s.find();
        s.publish();
        assert(!find(s, "ReferenceNozzle N1 is missing a vacuum valve actuator."));
        S::Issue* cmd = const_cast<S::Issue*>(find(s, "The vacuum valve actuator Vac has no ACTUATE_BOOLEAN_COMMAND assigned."));
        assert(cmd && cmd->solution == "Assign the command to driver Gantry as described in the Wiki.");
        std::string why;
        assert(!s.setState(*cmd, S::State::Solved, why) && !why.empty());   // nothing typed
        cmd->properties.front().setText("M8");
        assert(s.setState(*cmd, S::State::Solved, why) && cell.actuators.back().onCommand == "M8");
        cell.actuators.back().driverId.clear();   // no controller: said so instead
        s.find();
        s.publish();
        assert(find(s, "The vacuum valve actuator Vac has no driver assigned."));
        cell.actuators.back().http.on = true;     // worked by HTTP: needs none
        s.find();
        s.publish();
        assert(!find(s, "The vacuum valve actuator Vac has no driver assigned."));
    }
    // A contact probing nozzle, as OpenPnP's ContactProbeNozzle: its actuator,
    // on its Z's controller, and its probing command (suggested for a Grbl).
    {
        cell.nozzles[1].contactProbe.method = "ContactSenseActuator";
        s.find();
        s.publish();
        assert(find(s, "ContactProbeNozzle N2 has no contact probing actuator."));
        JPDriverConfig other;
        other.id = "D2";
        other.name = "Feeders";
        cell.drivers.push_back(other);
        JPActuatorConfig probe;
        probe.id = "P";
        probe.name = "Probe";
        probe.driverId = "D2";
        cell.actuators.push_back(probe);
        cell.nozzles[1].contactProbe.actuatorId = "P";
        s.find();
        s.publish();
        S::Issue* drv = const_cast<S::Issue*>(find(s, "Z driver Gantry not same as actuator Probe driver Feeders."));
        std::string why;
        assert(drv && s.setState(*drv, S::State::Solved, why) && cell.actuators.back().driverId == "D");
        s.find();
        s.publish();
        assert(find(s, "Missing ACTUATE_BOOLEAN_COMMAND for actuator Probe on driver Gantry (no suggestion available for "
                       "detected firmware)."));
        cell.drivers[0].profile = "grbl";
        cell.axes[2].softLimitLowEnabled = true;
        cell.axes[2].softLimitLow = -30;   // probed to 1 mm past it (depth 2 less start offset 1), at 5% of 100 mm/s
        s.find();
        s.publish();
        S::Issue* cmd = const_cast<S::Issue*>(find(s, "ACTUATE_BOOLEAN_COMMAND suggested."));
        assert(cmd && s.setState(*cmd, S::State::Solved, why) && cell.actuators.back().onCommand == "G38.2 Z-31 F300");
        s.find();
        s.publish();
        assert(!find(s, "ACTUATE_BOOLEAN_COMMAND suggested."));
        cell.drivers[0].profile = "auto";
        cell.axes[2].softLimitLowEnabled = false;
        cell.axes[2].softLimitLow = 0;
        cell.nozzles[1].contactProbe.method = "None";
        cell.drivers.pop_back();
        cell.actuators.pop_back();
    }
    // The letter set from the issue itself.
    S::Issue* letter = const_cast<S::Issue*>(find(s, "Axis letter is missing. Assign the letter to continue."));
    letter->properties.front().setText("Z");
    assert(cell.axes[2].letter == "Z");

    // Kinematics: home, Safe Z, soft limits, the acceleration, rotation.
    s.setTargetMilestone(S::Milestone::Kinematics);
    s.find();
    s.publish();
    S::Issue* home = const_cast<S::Issue*>(find(s, "To continue, the machine must be enabled and homed."));
    assert(home && find(s, "An acceleration limit must be set on axis c."));
    assert(find(s, "Rotation can be optimized by wrapping-around the shorter way. Best combined with Limit ±180°."));
    std::string why;
    assert(s.setState(*home, S::State::Solved, why) && homed);
    S::Issue* safeZ = const_cast<S::Issue*>(find(s, "Set Safe Z of N1."));
    assert(safeZ && s.setState(*safeZ, S::State::Solved, why));
    assert(cell.axes[2].safeZoneLowEnabled && cell.axes[2].safeZoneLow == 12.5);
    assert(s.setState(*safeZ, S::State::Open, why) && !cell.axes[2].safeZoneLowEnabled);
    S::Issue* low = const_cast<S::Issue*>(find(s, "Set the low side soft limit of x."));
    assert(low && s.setState(*low, S::State::Solved, why));
    assert(cell.axes[0].softLimitLowEnabled && cell.axes[0].softLimitLow == -3);
    // Not known where y is: refused, and why.
    S::Issue* yLow = const_cast<S::Issue*>(find(s, "Set the low side soft limit of y."));
    assert(yLow && !s.setState(*yLow, S::State::Solved, why) && !why.empty());
    // Rotation wrap-around set on Accept.
    S::Issue* wrap = const_cast<S::Issue*>(
        find(s, "Rotation can be optimized by wrapping-around the shorter way. Best combined with Limit ±180°."));
    assert(s.setState(*wrap, S::State::Solved, why) && cell.axes[3].wrapAroundRotation);
    // A rotation axis limited to less than a turn: Limited Articulation set on
    // Accept, and bottom vision made to pre-rotate (the machine's setting,
    // and a part's vision settings that never would).
    {
        const std::string limitedText = "Rotation axis c is limiting Nozzle N1 to less than 360°. Must use the "
                                        "LimitedArticulation rotation mode.";
        const std::string preRotateText = "Pre-rotate bottom vision must be enabled, because the machine has a "
                                          "limited articulation nozzle.";
        const std::string usageText = "Pre-rotate bottom vision must be allowed on all vision settings, because the "
                                      "machine has a limited articulation nozzle.";
        JPConfiguration config("");   // not read or saved here
        JPVisionSettings off = JPVisionSettings::create(JPVisionSettings::Kind::Bottom, "BVS1");
        off.name = "Tall";
        off.setText("pre-rotate-usage", "AlwaysOff");
        config.addVisionSettings(off);
        int configChanges = 0;
        JPIssueChecks::Context lc = c;
        lc.config = &config;
        lc.configurationChanged = [&configChanges] { ++configChanges; };
        S ls;
        ls.setChecks(JPIssueChecks::all(lc));
        ls.setTargetMilestone(S::Milestone::Kinematics);
        ls.find();
        ls.publish();
        assert(!find(ls, limitedText) && !find(ls, preRotateText));   // ±180°: a whole turn
        cell.axes[3].limitRotation = true;
        cell.axes[3].softLimitLowEnabled = cell.axes[3].softLimitHighEnabled = true;
        cell.axes[3].softLimitLow = -90;
        cell.axes[3].softLimitHigh = 90;
        cell.vision.preRotate = false;
        ls.find();
        ls.publish();
        S::Issue* limited = const_cast<S::Issue*>(find(ls, limitedText));
        assert(limited && limited->severity == S::Severity::Error);
        assert(ls.setState(*limited, S::State::Solved, why) && cell.nozzles[0].rotationMode == "LimitedArticulation");
        S::Issue* pre = const_cast<S::Issue*>(find(ls, preRotateText));
        assert(pre && ls.setState(*pre, S::State::Solved, why) && cell.vision.preRotate);
        S::Issue* usage = const_cast<S::Issue*>(find(ls, usageText));
        assert(usage && usage->extendedDescription.find("1. Tall") != std::string::npos);
        assert(ls.setState(*usage, S::State::Solved, why) && configChanges == 1);
        assert(config.visionSettings("BVS1")->text("pre-rotate-usage") == "Default");
        assert(ls.setState(*usage, S::State::Open, why));
        assert(config.visionSettings("BVS1")->text("pre-rotate-usage") == "AlwaysOff");
        assert(ls.setState(*limited, S::State::Open, why) && cell.nozzles[0].rotationMode == "AbsolutePartAngle");
        assert(ls.setState(*pre, S::State::Open, why) && !cell.vision.preRotate);
        cell.nozzles[0].rotationMode = "LimitedArticulation";   // chosen: no longer an issue, pre-rotate still is
        ls.find();
        ls.publish();
        assert(!find(ls, limitedText) && find(ls, preRotateText));
        cell.nozzles[0].rotationMode = "AbsolutePartAngle";
        cell.vision.preRotate = true;
        cell.axes[3].limitRotation = false;
        cell.axes[3].softLimitLowEnabled = cell.axes[3].softLimitHighEnabled = false;
    }
    // Calibration: a nozzle tip's part diameters and pick tolerance, as OpenPnP's NozzleTipSolutions.
    {
        JPNozzleTipConfig t;
        t.id = "T";
        t.name = "T1";
        cell.nozzleTips.push_back(t);   // Min. Part Diameter 0: not more than twice the 1 mm pick tolerance
        cell.nozzles[0].tipIds = { "T" };
        s.setTargetMilestone(S::Milestone::Calibration);
        s.find();
        s.publish();
        assert(find(s, "Nozzle tip T1 has an invalid Min. Part Diameter of 0.000 mm."));
        cell.nozzleTips[0].minPartDiameterMm = 3;
        cell.nozzleTips[0].maxPartDiameterMm = 3;
        s.find();
        s.publish();
        assert(find(s, "Nozzle tip T1 has a Max. Part Diameter that is not larger than the Min. Part Diameter."));
    }
    // Calibration solved by work on the machine: Accept starts it; failing at once, Accept fails; failing later, open again.
    {
        JPCameraConfig cam;
        cam.id = "T";
        cam.name = "Down";
        cam.mount = { "H", "x", "y", "", "" };
        JPCellConfig withCamera = cell;
        withCamera.cameras.push_back(cam);
        withCamera.heads.front().homingFiducial = JPMachineLocation { 10, 10, 0, 0 };
        withCamera.axes[0].kind = JPAxisConfig::Kind::Controller;
        withCamera.axes[1].kind = JPAxisConfig::Kind::Controller;
        JPIssueChecks::Context k = c;
        k.cell = [&withCamera]() -> const JPCellConfig* { return &withCamera; };
        std::function<void(bool)> later;
        bool atOnce = false;
        k.calibrateBacklash = [&](const std::string&, std::function<void(bool)> finished) {
            if (atOnce) finished(false);
            else later = std::move(finished);
        };
        S b;
        b.setChecks(JPIssueChecks::all(k));
        b.setTargetMilestone(S::Milestone::Calibration);
        b.find();
        b.publish();
        S::Issue* x = const_cast<S::Issue*>(find(b, "Calibrate backlash compensation for axis x."));
        assert(x && find(b, "Calibrate backlash compensation for axis y.") && x->severity == S::Severity::Fundamental);
        std::string why;
        assert(b.setState(*x, S::State::Solved, why) && x->state == S::State::Solved && later);
        later(false);   // failed on the machine
        x = const_cast<S::Issue*>(find(b, "Calibrate backlash compensation for axis x."));
        assert(x->state == S::State::Open);
        atOnce = true;
        assert(!b.setState(*x, S::State::Solved, why) && x->state == S::State::Open && !why.empty());
    }
    // OpenPnP's CameraSolutions on the device's own settings: each to its raw value, none automatic.
    {
        JPCellConfig p2 = cell;
        JPCameraConfig cam;
        cam.id = "U";
        cam.name = "Up";
        cam.device = JJson::object();
        cam.device["backend"] = "v4l2";
        p2.cameras.push_back(cam);
        JPIssueChecks::Context k = c;
        k.cell = [&p2]() -> const JPCellConfig* { return &p2; };
        k.changeCell = [&p2](const std::string&, const std::function<void(JPCellConfig&)>& edit) { edit(p2); };
        k.cameraControls = [](const std::string&) {
            return JJson::parse(R"({ "brightness": { "value": 10, "min": -64, "max": 64, "default": 0 },
                                     "sharpness": { "value": 3, "min": 0, "max": 7, "default": 3 },
                                     "contrast": { "value": 32, "min": 0, "max": 64, "default": 32 },
                                     "exposure": { "value": 157, "min": 1, "max": 5000, "default": 157, "auto": true } })");
        };
        S cp;
        cp.setChecks(JPIssueChecks::all(k));
        cp.setTargetMilestone(S::Milestone::Vision);
        cp.find();
        cp.publish();
        S::Issue* bright = const_cast<S::Issue*>(find(cp, "The brightness of camera Up should be set to 0."));
        S::Issue* sharp = const_cast<S::Issue*>(find(cp, "The sharpness of camera Up should be set to 0."));
        S::Issue* exposure = const_cast<S::Issue*>(find(cp, "The exposure of camera Up should not be set to Auto."));
        assert(bright && sharp && exposure && !find(cp, "The contrast of camera Up should be set to 32."));
        assert(bright->solution.find("Therefore, revert to the default setting.") != std::string::npos);
        std::string why;
        assert(cp.setState(*exposure, S::State::Solved, why));
        const JJson& set = std::as_const(p2.cameras.back().device)["controls"]["exposure"];
        assert(!set["auto"].boolean() && set["value"].number() == 157);
        assert(cp.setState(*exposure, S::State::Open, why) && !std::as_const(p2.cameras.back().device)["controls"]["exposure"].isObject());
    }
    // OpenPnP's VisionSolutions: Enable Visual Homing (the mark under the camera on Accept), the rig's heights.
    {
        JPCameraConfig cam;
        cam.id = "T";
        cam.name = "Down";
        cam.mount = { "H", "x", "y", "", "" };
        JPCellConfig v = cell;
        v.cameras.push_back(cam);
        v.heads.front().rigPrimary = JPMachineLocation { 0, 0, -10, 0 };
        v.heads.front().rigSecondary = JPMachineLocation { 5, 0, -9, 0 };
        v.axes[2].safeZoneLowEnabled = true;
        v.axes[2].safeZoneLow = -9.5;   // the secondary fiducial above it
        JPIssueChecks::Context k = c;
        k.cell = [&v]() -> const JPCellConfig* { return &v; };
        std::string enabled;
        k.enableVisualHoming = [&enabled](const std::string& head, std::function<void(bool)> finished) {
            enabled = head;
            finished(true);
        };
        S vs;
        vs.setChecks(JPIssueChecks::all(k));
        vs.setTargetMilestone(S::Milestone::Vision);
        vs.find();
        vs.publish();
        S::Issue* home = const_cast<S::Issue*>(find(vs, "Enable Visual Homing."));
        assert(home && home->severity == S::Severity::Suggestion);
        std::string why;
        assert(vs.setState(*home, S::State::Solved, why) && enabled == "H");
        assert(find(vs, "Primary/secondary calibration fiducial Z too close together."));
        assert(find(vs, "Safe Z of Nozzle N1 lower than secondary fiducial Z."));
        assert(!find(vs, "Safe Z of Nozzle N1 lower than primary fiducial Z."));
    }
    // OpenPnP's KinematicSolutions: Safe Z dynamic or fixed (the choice taken on Accept), positive Safe Z, the tallest part.
    {
        JPCellConfig k2 = cell;
        k2.axes[2].safeZoneLowEnabled = k2.axes[2].safeZoneHighEnabled = true;
        k2.axes[2].safeZoneLow = 3;
        k2.axes[2].safeZoneHigh = 5;
        JPNozzleTipConfig t;
        t.id = "T9";
        t.name = "T9";
        t.maxPartHeightMm = 4;
        k2.nozzleTips.push_back(t);
        k2.nozzles[0].tipIds = { "T9" };
        k2.nozzles[0].dynamicSafeZ = true;
        JPIssueChecks::Context k = c;
        k.cell = [&k2]() -> const JPCellConfig* { return &k2; };
        k.changeCell = [&k2](const std::string&, const std::function<void(JPCellConfig&)>& edit) { edit(k2); };
        k.axisPosition = [](const std::string& id) -> std::optional<double> {
            if (id == "x") return -3;
            if (id == "y") return 7;
            if (id == "z") return 12.5;
            return std::nullopt;
        };
        S kin;
        kin.setChecks(JPIssueChecks::all(k));
        kin.setTargetMilestone(S::Milestone::Kinematics);
        kin.find();
        kin.publish();
        assert(find(kin, "Unconventional Z Axis on N1."));
        assert(find(kin, "Nozzle N1 with tip T9 Safe Z Zone violation."));
        S::Issue* dyn = const_cast<S::Issue*>(find(kin, "Dynamic Safe Z for N2."));
        assert(dyn && dyn->choice == "Fixed Safe Z" && dyn->choices.size() == 2);
        dyn->choice = "Dynamic Safe Z";
        std::string why;
        assert(kin.setState(*dyn, S::State::Solved, why) && k2.nozzles[1].dynamicSafeZ);
        assert(kin.setState(*dyn, S::State::Open, why) && !k2.nozzles[1].dynamicSafeZ);
        // The manual tip change location, from where the nozzle is.
        S::Issue* manual = const_cast<S::Issue*>(find(kin, "Set the manual nozzle tip change location for N1."));
        assert(manual && kin.setState(*manual, S::State::Solved, why));
        assert(k2.nozzles[0].manualChangeLocation && k2.nozzles[0].manualChangeLocation->x == -3
               && k2.nozzles[0].manualChangeLocation->z == 12.5);
    }
    // A tip's background calibration: the method chosen, then the tip calibrated.
    {
        JPCellConfig b2 = cell;
        JPNozzleTipConfig t;
        t.id = "T8";
        t.name = "T8";
        b2.nozzleTips.push_back(t);
        JPIssueChecks::Context k = c;
        k.cell = [&b2]() -> const JPCellConfig* { return &b2; };
        k.changeCell = [&b2](const std::string&, const std::function<void(JPCellConfig&)>& edit) { edit(b2); };
        std::string calibrated;
        k.calibrateTip = [&calibrated](const std::string& id, std::function<void(bool)> finished) {
            calibrated = id;
            finished(true);
        };
        S bg;
        bg.setChecks(JPIssueChecks::all(k));
        bg.setTargetMilestone(S::Milestone::Calibration);
        bg.find();
        bg.publish();
        S::Issue* method = const_cast<S::Issue*>(find(bg, "Set background calibration method for T8."));
        assert(method && method->choices.size() == 2);
        method->choice = "Brightness";
        std::string why;
        assert(bg.setState(*method, S::State::Solved, why) && calibrated == "T8" && b2.nozzleTips.back().background.method == "Brightness");
    }
    // OpenPnP's HeadSolutions: a nozzle on other X/Y axes than the head's camera, put on the camera's on Accept.
    {
        JPCellConfig h2 = cell;
        JPCameraConfig cam;
        cam.id = "T";
        cam.name = "Down";
        cam.mount = { "H", "x", "y", "", "" };
        h2.cameras.push_back(cam);
        h2.nozzles[1].mount.axisY = "z";
        JPIssueChecks::Context k = c;
        k.cell = [&h2]() -> const JPCellConfig* { return &h2; };
        k.changeCell = [&h2](const std::string&, const std::function<void(JPCellConfig&)>& edit) { edit(h2); };
        S hs;
        hs.setChecks(JPIssueChecks::all(k));
        hs.setTargetMilestone(S::Milestone::Basics);
        hs.find();
        hs.publish();
        S::Issue* y = const_cast<S::Issue*>(find(hs, "Inconsistent Y axis assignment z (not the same as default camera Down)."));
        assert(y && y->severity == S::Severity::Error && y->solution == "Assign y as the Y axis.");
        assert(!find(hs, "Inconsistent X axis assignment x (not the same as default camera Down)."));
        std::string why;
        assert(hs.setState(*y, S::State::Solved, why) && h2.nozzles[1].mount.axisY == "y");
    }
    // OpenPnP's GcodeDriverSolutions: flow control for a Grbl, pre-move commands, the maximum feed rate, compression.
    {
        JPCellConfig g = cell;
        g.drivers.front().profile = "grblhal";
        g.drivers.front().link["type"] = std::string("serial");
        g.drivers.front().link["flowControl"] = std::string("rtscts");
        g.drivers.front().supportingPreMove = true;
        g.drivers.front().maxFeedRate = 5000;
        JPIssueChecks::Context k = c;
        k.cell = [&g]() -> const JPCellConfig* { return &g; };
        k.changeCell = [&g](const std::string&, const std::function<void(JPCellConfig&)>& edit) { edit(g); };
        S d;
        d.setChecks(JPIssueChecks::all(k));
        d.setTargetMilestone(S::Milestone::Kinematics);
        d.find();
        d.publish();
        S::Issue* flow = const_cast<S::Issue*>(find(d, "Change of serial port Flow Control recommended."));
        S::Issue* premove = const_cast<S::Issue*>(find(d, "Disallow Pre-Move Commands for automatic G-code setup and other advanced features. Accept or Dismiss to continue."));
        S::Issue* feed = const_cast<S::Issue*>(find(d, "Axis velocity limited by driver Maximum Feed Rate. "));
        assert(flow && premove && feed && !find(d, "Compress Gcode for superior communications speed."));
        std::string why;
        assert(d.setState(*flow, S::State::Solved, why) && g.drivers.front().link["flowControl"].str().empty());
        assert(d.setState(*premove, S::State::Solved, why) && !g.drivers.front().supportingPreMove);
        assert(d.setState(*feed, S::State::Solved, why) && g.drivers.front().maxFeedRate == 0);
        assert(d.setState(*feed, S::State::Open, why) && g.drivers.front().maxFeedRate == 5000);
        d.setTargetMilestone(S::Milestone::Advanced);
        d.find();
        d.publish();
        S::Issue* compress = const_cast<S::Issue*>(find(d, "Compress Gcode for superior communications speed."));
        assert(compress && d.setState(*compress, S::State::Solved, why) && g.drivers.front().compressGcode);
    }
    // Production: the tables linked on Accept, unlinked again on Reopen.
    {
        bool linked = false;
        JPIssueChecks::Context k = c;
        k.tablesLinked = [&linked] { return linked; };
        k.setTablesLinked = [&linked](bool on) { linked = on; };
        S p;
        p.setChecks(JPIssueChecks::all(k));
        p.setTargetMilestone(S::Milestone::Production);
        p.find();
        p.publish();
        S::Issue* link = const_cast<S::Issue*>(find(p, "Link the Placements/Parts/Packages/Vision Settings/Feeders tables between tabs."));
        std::string why;
        assert(link && p.setState(*link, S::State::Solved, why) && linked);
        assert(p.setState(*link, S::State::Open, why) && !linked);
    }
    // The cameras' preview, as OpenPnP's CameraSolutions: its rate, suspended in tasks, brought forward, drawn smoothed.
    {
        JPCameraConfig cam;
        cam.id = "C";
        cam.name = "Top";
        cam.previewFps = 30;
        cam.device = JJson::object();
        cam.device["backend"] = "switcher";
        cell.cameras.push_back(cam);
        bool smooth = false;
        c.renderingSmooth = [&smooth](const std::string&) { return smooth; };
        c.setRenderingSmooth = [&smooth](const std::string&, bool on) { smooth = on; };
        S v;
        v.setChecks(JPIssueChecks::all(c));
        v.setTargetMilestone(S::Milestone::Vision);
        v.find();
        v.publish();
        S::Issue* fps = const_cast<S::Issue*>(find(v, "A high Preview FPS value might create undue CPU load."));
        S::Issue* suspend = const_cast<S::Issue*>(find(v, "For a SwitcherCamera it is mandatory to suspend camera preview during machine tasks / Jobs."));
        S::Issue* quality = const_cast<S::Issue*>(find(v, "The preview rendering quality can be improved."));
        assert(fps && suspend && suspend->severity == S::Severity::Error && quality);
        assert(find(v, "In single camera preview jplacer can automatically switch the camera for you."));
        std::string why;
        assert(v.setState(*fps, S::State::Solved, why) && cell.cameras.back().previewFps == 5);
        assert(v.setState(*suspend, S::State::Solved, why) && cell.cameras.back().suspendDuringTasks);
        assert(v.setState(*quality, S::State::Solved, why) && smooth);
        assert(v.setState(*fps, S::State::Open, why) && cell.cameras.back().previewFps == 30);   // undone
    }
    return 0;
}
