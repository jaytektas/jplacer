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
    return 0;
}
