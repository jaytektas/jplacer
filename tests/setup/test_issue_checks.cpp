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
