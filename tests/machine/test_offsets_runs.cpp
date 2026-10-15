// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// A nozzle's offsets calibrations, run by run (JPOffsetsRun, JPNozzleConfig::calibrated): a run takes its offsets,
// the ones before filled in (at the run's height); the newest kept last, JPOffsetsRun::kKept at most; kept in the
// cell file and read back with each height's estimates. An angle further from its height's mean than 3 times their
// median distance (at least 0.01 mm) is an outlier. A precise run at two heights sets the nozzle's lean (its offsets
// at the first height, JPMountConfig::offsetXAt elsewhere); one at one height keeps the lean, re-based to its height;
// a rough one leaves it. A nozzle file without runs or a lean reads none.
// Tests check with assert(); a Release build must not compile it away.
#undef NDEBUG
#include <cassert>

#include "machine/JPNozzleConfig.h"

#include <cmath>
#include <string>

using namespace jf;

namespace {

bool near(double a, double b) { return std::abs(a - b) < 1e-9; }

JPOffsetsRun precise(double afterX, double afterY) {
    JPOffsetsRun r;
    r.when = "2026-10-15 10:00";
    r.how = JPOffsetsRun::kPrecise;
    r.afterX = afterX;
    r.afterY = afterY;
    return r;
}

} // namespace

int main() {
    JPNozzleConfig n;
    n.id = "N1";
    n.mount.offsetX = -19.9;
    n.mount.offsetY = -61.9;
    n.mount.offsetZ = 1.5;

    // Six angles about a correction of +0.02, -0.01 at Z -24; one 0.05 mm off on its own.
    JPOffsetsRun r = precise(-19.88, -61.91);
    const double cx = 0.02, cy = -0.01;
    r.first.z = -24;
    r.first.angles = { { -150, cx + 0.001, cy }, { -90, cx, cy + 0.002 }, { -30, cx - 0.001, cy - 0.001 },
                       { 30, cx + 0.05, cy }, { 90, cx, cy - 0.002 }, { 150, cx - 0.002, cy + 0.001 } };
    n.calibrated(r);
    assert(near(n.mount.offsetX, -19.88) && near(n.mount.offsetY, -61.91) && near(n.mount.offsetZ, 1.5));
    assert(n.offsetsWhen == r.when && n.offsetsHow == JPOffsetsRun::kPrecise);
    assert(n.offsetsRuns.size() == 1 && !n.mount.leans() && n.mount.leanZ == -24);
    const JPOffsetsRun kept = n.offsetsRuns.back();
    assert(near(kept.beforeX, -19.9) && near(kept.beforeY, -61.9));
    const JPOffsetsRun::Height& h = kept.first;
    assert(near(h.meanX(), cx + 0.048 / 6) && h.outliers() == 1 && h.outlier(h.angles[3]) && !h.outlier(h.angles[0]));
    assert(kept.outliers() == 1 && kept.angles() == 6);
    // Angles agreeing to the micron: the limit is not below 0.01 mm, so none is an outlier.
    JPOffsetsRun::Height tight { 0, { { 0, 0.0001, 0 }, { 90, 0, 0.0001 }, { 180, -0.0001, 0 } } };
    assert(near(tight.outlierLimit(), JPOffsetsRun::kLeastOutlierMm) && tight.outliers() == 0);
    // A run without angles (by a mark, on the fiducial): no outliers, no spread.
    JPOffsetsRun mark = precise(1, 1);
    mark.how = JPOffsetsRun::kByMark;
    assert(mark.outliers() == 0 && mark.spread() == 0 && mark.angles() == 0);

    // At two heights: the offsets at the first, the lean between them; the offsets at any height from there.
    JPOffsetsRun two = precise(-19.87, -61.92);
    two.first = { -24, { { 0, 0.01, -0.01 }, { 180, 0.01, -0.01 } } };
    two.second = JPOffsetsRun::Height { -13, { { 0, 0.02, 0.01 }, { 180, 0.02, 0.01 } } };
    two.leanX = 0.002;
    two.leanY = -0.001;
    n.calibrated(two);
    assert(n.mount.leans() && n.mount.leanX == 0.002 && n.mount.leanY == -0.001 && n.mount.leanZ == -24);
    assert(near(n.mount.offsetXAt(-24), -19.87) && near(n.mount.offsetXAt(-13), -19.87 + 0.022)
           && near(n.mount.offsetYAt(-14), -61.92 - 0.01));
    // Another at one height, Z -22: the lean kept, the offsets there, and before them the offsets at -22 as they were.
    JPOffsetsRun one = precise(-19.85, -61.9);
    one.first = { -22, { { 0, 0, 0 } } };
    one.leanX = n.mount.leanX;
    one.leanY = n.mount.leanY;
    n.calibrated(one);
    assert(near(n.offsetsRuns.back().beforeX, -19.87 + 0.004) && n.mount.leanZ == -22 && n.mount.leanX == 0.002
           && near(n.mount.offsetXAt(-22), -19.85));
    // A rough one: the offsets as they are kept, the lean left.
    JPOffsetsRun rough = precise(-19.8, -61.9);
    rough.how = JPOffsetsRun::kOnFiducial;
    n.calibrated(rough);
    assert(n.mount.leanX == 0.002 && n.mount.leanZ == -22 && near(n.mount.offsetX, -19.8));

    // Kept in the file, and read back the same: the runs with their heights, the nozzle's lean.
    const JPNozzleConfig back = JPNozzleConfig::fromJson(n.toJson());
    assert(back.offsetsRuns.size() == 4);
    const JPOffsetsRun& b0 = back.offsetsRuns[0];
    assert(b0.when == kept.when && b0.how == kept.how && near(b0.beforeX, kept.beforeX) && near(b0.afterY, kept.afterY));
    assert(b0.first.z == -24 && b0.first.angles.size() == 6 && near(b0.first.angles[3].dx, cx + 0.05) && b0.outliers() == 1
           && !b0.second);
    const JPOffsetsRun& b1 = back.offsetsRuns[1];
    assert(b1.second && b1.second->z == -13 && b1.second->angles.size() == 2 && b1.leanX == 0.002 && b1.leanY == -0.001);
    assert(back.mount.leanX == 0.002 && back.mount.leanY == -0.001 && back.mount.leanZ == -22);
    assert(back.offsetsWhen == n.offsetsWhen && back.offsetsHow == n.offsetsHow);

    // The newest kept, kKept at most.
    for (size_t i = 0; i < JPOffsetsRun::kKept + 3; ++i) n.calibrated(precise(double(i), 0));
    assert(n.offsetsRuns.size() == JPOffsetsRun::kKept);
    assert(near(n.offsetsRuns.back().afterX, double(JPOffsetsRun::kKept + 2)));
    assert(near(n.offsetsRuns.back().beforeX, double(JPOffsetsRun::kKept + 1)));

    // A nozzle from before runs or a lean were kept: none.
    JJson old = JJson::object();
    old["id"] = "N2";
    old["offsetsCalibrated"]["when"] = "2026-09-01 09:00";
    old["offsetsCalibrated"]["how"] = JPOffsetsRun::kOnFiducial;
    const JPNozzleConfig o = JPNozzleConfig::fromJson(old);
    assert(o.offsetsRuns.empty() && o.offsetsHow == JPOffsetsRun::kOnFiducial && !o.mount.leans());
    return 0;
}
