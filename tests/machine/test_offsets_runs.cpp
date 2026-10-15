// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// A nozzle's offsets calibrations, run by run (JPOffsetsRun, JPNozzleConfig::calibrated): a run takes its offsets,
// the ones before filled in; the newest kept last, JPOffsetsRun::kKept at most; kept in the cell file and read back
// with each angle's estimate. An angle further from the correction than 3 times their median distance (at least
// 0.01 mm) is an outlier. A nozzle file without runs reads none.
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

    // Six angles about a correction of +0.02, -0.01; one 0.05 mm off on its own.
    JPOffsetsRun r = precise(-19.88, -61.91);
    const double cx = 0.02, cy = -0.01;
    r.angles = { { -150, cx + 0.001, cy }, { -90, cx, cy + 0.002 }, { -30, cx - 0.001, cy - 0.001 },
                 { 30, cx + 0.05, cy }, { 90, cx, cy - 0.002 }, { 150, cx - 0.002, cy + 0.001 } };
    n.calibrated(r);
    assert(near(n.mount.offsetX, -19.88) && near(n.mount.offsetY, -61.91) && near(n.mount.offsetZ, 1.5));
    assert(n.offsetsWhen == r.when && n.offsetsHow == JPOffsetsRun::kPrecise);
    assert(n.offsetsRuns.size() == 1);
    const JPOffsetsRun& kept = n.offsetsRuns.back();
    assert(near(kept.beforeX, -19.9) && near(kept.beforeY, -61.9));
    assert(near(kept.changeX(), 0.02) && near(kept.changeY(), -0.01));
    assert(kept.outliers() == 1 && kept.outlier(kept.angles[3]) && !kept.outlier(kept.angles[0]));
    assert(near(kept.spread(), 0.05));
    // Angles agreeing to the micron: the limit is not below 0.01 mm, so none is an outlier.
    JPOffsetsRun tight = precise(0, 0);
    tight.angles = { { 0, 0.0001, 0 }, { 90, 0, 0.0001 }, { 180, -0.0001, 0 } };
    assert(near(tight.outlierLimit(), JPOffsetsRun::kLeastOutlierMm) && tight.outliers() == 0);
    // A run without angles (by a mark, on the fiducial): no outliers, no spread.
    JPOffsetsRun mark = precise(1, 1);
    mark.how = JPOffsetsRun::kByMark;
    assert(mark.outliers() == 0 && mark.spread() == 0);

    // Kept in the file, and read back the same.
    const JPNozzleConfig back = JPNozzleConfig::fromJson(n.toJson());
    assert(back.offsetsRuns.size() == 1);
    const JPOffsetsRun& b = back.offsetsRuns[0];
    assert(b.when == kept.when && b.how == kept.how && near(b.beforeX, kept.beforeX) && near(b.afterY, kept.afterY));
    assert(b.angles.size() == 6 && near(b.angles[3].angle, 30) && near(b.angles[3].dx, cx + 0.05) && b.outliers() == 1);
    assert(back.offsetsWhen == n.offsetsWhen && back.offsetsHow == n.offsetsHow);

    // The newest kept, kKept at most.
    for (size_t i = 0; i < JPOffsetsRun::kKept + 3; ++i) n.calibrated(precise(double(i), 0));
    assert(n.offsetsRuns.size() == JPOffsetsRun::kKept);
    assert(near(n.offsetsRuns.back().afterX, double(JPOffsetsRun::kKept + 2)));
    assert(near(n.offsetsRuns.back().beforeX, double(JPOffsetsRun::kKept + 1)));

    // A nozzle from before runs were kept: none.
    JJson old = JJson::object();
    old["id"] = "N2";
    old["offsetsCalibrated"]["when"] = "2026-09-01 09:00";
    old["offsetsCalibrated"]["how"] = JPOffsetsRun::kOnFiducial;
    const JPNozzleConfig o = JPNozzleConfig::fromJson(old);
    assert(o.offsetsRuns.empty() && o.offsetsHow == JPOffsetsRun::kOnFiducial);
    return 0;
}
