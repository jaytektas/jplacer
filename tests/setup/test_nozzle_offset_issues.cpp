// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// OpenPnP's nozzle offset issues in Issues & Solutions. Vision: "Nozzle N offsets for the primary fiducial." for
// each nozzle once the calibration rig's primary fiducial X, Y is known (and its Z, but for the head's first
// nozzle), and for the first nozzle the secondary fiducial's too; Accept with the nozzle tip touching the fiducial
// takes the fiducial's Z (the first nozzle) and the offsets (the fiducial less where the axes are), keeping X, Y
// that are already within half the fiducial (perhaps calibrated precisely); refused above Safe Z, and the two
// fiducials' Z too close; undone, as they were. Calibration: "Calibrate precise camera <-> nozzle N offsets." once
// the offsets are roughly set, its Feature diameter (from the test object's size by the camera's scale; each change
// previewed), its Auto-Detect Next (the diameter found taken, the issue shown again), and Accept: the calibration
// with the feature diameter, its result shown with the issue. Capture Test Object Z: the nozzle's Z as it touches the
// test object, kept as the rig's test object height (shown with the issue; not captured, the primary fiducial's Z).
// Tests check with assert(); a Release build must not compile it away.
#undef NDEBUG
#include <cassert>

#include "setup/JPIssueChecks.h"

#include <cmath>
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

S::Issue* find(S& s, const std::string& issue) {
    for (const auto& i : s.issues())
        if (i->issue == issue) return i.get();
    return nullptr;
}

bool near(double a, double b) { return std::abs(a - b) < 1e-9; }

} // namespace

int main() {
    JPCellConfig cell;
    JPDriverConfig d;
    d.id = "D";
    d.name = "Gantry";
    cell.drivers.push_back(d);
    cell.axes = { axis("x", JPAxisConfig::Type::X, "X"), axis("y", JPAxisConfig::Type::Y, "Y"), axis("z", JPAxisConfig::Type::Z, "Z"),
                  axis("c", JPAxisConfig::Type::Rotation, "A"), axis("z2", JPAxisConfig::Type::Z, "B"), axis("c2", JPAxisConfig::Type::Rotation, "C") };
    for (const int k : { 2, 4 }) {
        cell.axes[size_t(k)].safeZoneLowEnabled = true;
        cell.axes[size_t(k)].safeZoneLow = -1;
    }
    JPHeadConfig h;
    h.id = "H";
    h.name = "H1";
    h.rigPrimary = JPMachineLocation { 137.137, 179.265, 0, 0 };
    h.rigPrimaryDiameter = 1.85;
    h.rigTestObjectDiameter = 7.5;
    cell.heads.push_back(h);
    JPCameraConfig cam;
    cam.id = "C";
    cam.name = "TOP";
    cam.mount = { "H", "x", "y", "", "" };
    JPCameraCalibration cal;
    cal.width = 1280;
    cal.height = 720;
    cam.calibrations.push_back(cal);
    cell.cameras.push_back(cam);
    JPNozzleConfig left, right;
    left.id = "L";
    left.name = "LEFT";
    left.mount = { "H", "x", "y", "z", "c" };
    right.id = "R";
    right.name = "RIGHT";
    right.mount = { "H", "x", "y", "z2", "c2" };
    cell.nozzles = { left, right };

    std::map<std::string, double> at { { "x", 157.0 }, { "y", 241.2 }, { "z", -23.6 }, { "z2", -23.4 } };
    int previewed = -1, calibratedAt = -1;
    std::string chosen;
    std::optional<JPIssueChecks::Context::OffsetsResult> result;
    JPIssueChecks::Context c;
    c.cell = [&cell]() -> const JPCellConfig* { return &cell; };
    c.calibrated = [](const std::string&) { return true; };
    c.axisPosition = [&at](const std::string& id) -> std::optional<double> {
        const auto i = at.find(id);
        return i == at.end() ? std::nullopt : std::optional(i->second);
    };
    c.changeCell = [&cell](const std::string&, const std::function<void(JPCellConfig&)>& edit) { edit(cell); };
    c.chooseNozzle = [&chosen](const std::string& id) { chosen = id; };
    c.previewFeature = [&previewed](int px) { previewed = px; };
    c.autoDetectFeature = [](int, std::function<void(std::optional<int>)> done) { done(244); };
    c.headCameraPixelsPerMm = [] { return std::optional(30.0); };
    c.calibratePreciseNozzleOffsets = [&](const std::string&, int px, std::function<void(bool)> finished) {
        calibratedAt = px;
        result = JPIssueChecks::Context::OffsetsResult { -19.925, -61.945, -19.3, -62.56 };
        finished(true);
    };
    c.nozzleOffsetsResult = [&result](const std::string&) { return result; };
    c.nozzleZ = [&at](const std::string& id) -> std::optional<double> { return id == "L" ? at["z"] - 0.4 : at["z2"]; };

    S s;
    s.setChecks(JPIssueChecks::all(c));
    s.setTargetMilestone(S::Milestone::Vision);
    s.find();
    s.publish();
    // The primary fiducial's Z not known: only the first nozzle's (it takes the Z), forced open.
    S::Issue* leftPrimary = find(s, "Nozzle LEFT offsets for the primary fiducial.");
    assert(leftPrimary && leftPrimary->forcedUnsolved && !find(s, "Nozzle RIGHT offsets for the primary fiducial."));
    assert(!find(s, "Nozzle LEFT offsets for the secondary fiducial."));
    leftPrimary->activate();
    assert(chosen == "L");
    std::string why;
    // Above Safe Z: refused.
    at["z"] = 0;
    assert(!s.setState(*leftPrimary, S::State::Solved, why) && why.find("lower than Safe Z") != std::string::npos);
    // Touching the fiducial: its Z taken, the offsets the fiducial less the axes.
    at["z"] = -23.6;
    assert(s.setState(*leftPrimary, S::State::Solved, why));
    assert(near(cell.heads[0].rigPrimary->z, -23.6));
    assert(near(cell.nozzles[0].mount.offsetX, 137.137 - 157.0) && near(cell.nozzles[0].mount.offsetY, 179.265 - 241.2)
           && near(cell.nozzles[0].mount.offsetZ, 0));
    // Undone: as they were.
    assert(s.setState(*leftPrimary, S::State::Open, why));
    assert(cell.nozzles[0].mount.offsetX == 0 && cell.heads[0].rigPrimary->z == 0);
    assert(s.setState(*leftPrimary, S::State::Solved, why));

    // The secondary fiducial known: its Z for the first nozzle (at least 2 mm from the primary's); the second nozzle's
    // offsets now (its Z equalized to the first's).
    cell.heads[0].rigSecondary = JPMachineLocation { 167.193, 179.308, 0, 0 };
    s.find();
    s.publish();
    S::Issue* secondary = find(s, "Nozzle LEFT offsets for the secondary fiducial.");
    assert(secondary);
    at["z"] = -22.6;
    assert(!s.setState(*secondary, S::State::Solved, why) && why.find("apart") != std::string::npos);
    at["z"] = -12.7;
    assert(s.setState(*secondary, S::State::Solved, why) && near(cell.heads[0].rigSecondary->z, -12.7));
    assert(near(cell.nozzles[0].mount.offsetX, 137.137 - 157.0));   // the secondary's: no offsets
    S::Issue* rightPrimary = find(s, "Nozzle RIGHT offsets for the primary fiducial.");
    assert(rightPrimary);
    at["x"] = 117.0;
    at["y"] = 241.1;
    assert(s.setState(*rightPrimary, S::State::Solved, why));
    assert(near(cell.nozzles[1].mount.offsetX, 137.137 - 117.0) && near(cell.nozzles[1].mount.offsetZ, -23.6 + 23.4));
    // Captured again (a new search's issue, the offsets still set) within half the fiducial of what they are: X, Y
    // kept (they may already be precise), Z taken.
    const double keptX = cell.nozzles[1].mount.offsetX, keptY = cell.nozzles[1].mount.offsetY;
    at["x"] = 117.3;
    at["z2"] = -23.5;
    s.setShowSolved(true);   // a solved issue is listed with Include Solved?
    s.find();
    s.publish();
    rightPrimary = find(s, "Nozzle RIGHT offsets for the primary fiducial.");
    assert(rightPrimary && s.setState(*rightPrimary, S::State::Open, why) && s.setState(*rightPrimary, S::State::Solved, why));
    assert(cell.nozzles[1].mount.offsetX == keptX && cell.nozzles[1].mount.offsetY == keptY);
    assert(near(cell.nozzles[1].mount.offsetZ, -23.6 + 23.5));
    // Further off than that: taken.
    at["x"] = 116.0;
    s.find();
    s.publish();
    rightPrimary = find(s, "Nozzle RIGHT offsets for the primary fiducial.");
    assert(rightPrimary && s.setState(*rightPrimary, S::State::Open, why) && s.setState(*rightPrimary, S::State::Solved, why));
    assert(near(cell.nozzles[1].mount.offsetX, 137.137 - 116.0));

    // Calibration: the precise offsets, now that they are roughly set.
    s.setTargetMilestone(S::Milestone::Calibration);
    s.find();
    s.publish();
    S::Issue* precise = find(s, "Calibrate precise camera ↔ nozzle LEFT offsets.");
    assert(precise && precise->properties.size() == 3);
    assert(precise->extendedDescription.find("Test object Z: -23.600 mm (the primary fiducial's") != std::string::npos);
    S::Property& diameter = precise->properties[0];
    assert(diameter.label == "Feature diameter" && diameter.getNumber() == 225);   // 7.5 mm at 30 px a mm
    assert(diameter.max == 503);   // 0.7 of the 720 px side, cut down as OpenPnP casts it
    diameter.setNumber(230);
    assert(previewed == 230);
    bool shownAgain = false;
    s.onSolutionChanged = [&shownAgain] { shownAgain = true; };
    precise->properties[1].action();
    assert(shownAgain && diameter.getNumber() == 244);
    // The test object's height: the nozzle tip touching it, captured (as a pick takes the nozzle's Z).
    assert(precise->properties[2].actionLabel == "Capture Test Object Z");
    at["z"] = -22.0;
    shownAgain = false;
    precise->properties[2].action();
    assert(shownAgain && cell.heads[0].rigTestObjectZ && near(*cell.heads[0].rigTestObjectZ, -22.4));
    assert(s.setState(*precise, S::State::Solved, why) && calibratedAt == 244);
    s.find();
    s.publish();
    precise = find(s, "Calibrate precise camera ↔ nozzle LEFT offsets.");
    assert(precise && precise->extendedDescription.find("Detected Nozzle Head Offsets: -19.3000, -62.5600 mm") != std::string::npos);
    assert(precise->extendedDescription.find("Test object Z: -22.400 mm (captured).") != std::string::npos);
    return 0;
}
