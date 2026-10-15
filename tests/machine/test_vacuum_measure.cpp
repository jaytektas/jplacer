// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// A tip's part detection ranges from measured readings (JPVacuumMeasure): a range is the mean, kSigmas
// deviations either side (at least the readings' spread and their resolution); a part on range stops at the
// boundary with the level without a part (as many of each one's deviations away); two too close to tell
// apart are said, and no part on range set. Part off sets its vacuum range from the level after the pulse (Absolute) or before
// it (Difference). A tip without a method is given Absolute. What was measured is kept in the tip's file.
// Tests check with assert(); a Release build must not compile it away.
#undef NDEBUG
#include <cassert>

#include "machine/JPNozzleTipConfig.h"
#include "machine/JPVacuumMeasure.h"

#include <cmath>

using namespace jf;

namespace {

bool near(double a, double b) { return std::abs(a - b) < 1e-6; }

} // namespace

int main() {
    using M = JPVacuumMeasure;

    // Readings 10, 12, 14: mean 12, deviation 2, resolution 2; the range 6 deviations either side.
    const M::Stats s = M::Stats::of({ 10, 12, 14 });
    assert(s.count == 3 && near(s.mean, 12) && near(s.sd, 2) && s.lowest == 10 && s.highest == 14 && s.resolution == 2);
    const M::Range r = M::around(s, std::nullopt);
    assert(near(r.low, 0) && near(r.high, 24) && !r.stopped);
    // All the same whole number: resolution 1, so half a unit of deviation (never a range of nothing).
    const M::Stats same = M::Stats::of({ -31000, -31000, -31000 });
    assert(same.sd == 0 && same.resolution == 1 && near(same.sigma(), 0.5) && near(same.halfWidth(), 3));
    // A wide spread with little deviation: the spread is the least half-width.
    const M::Stats spread = M::Stats::of({ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1 });
    assert(spread.halfWidth() >= 1);

    // Part on: with a part about 50000 (deviation 100), without about 20000 (deviation 100). The range would
    // reach 49400; the boundary is 35000, so it is not stopped. With a deviation of 3000 it would be, at the
    // point as many deviations from each.
    M m;
    std::vector<double> held, open;
    for (int i = 0; i < 20; ++i) {
        held.push_back(50000 + (i % 2 ? 100 : -100));
        open.push_back(20000 + (i % 2 ? 100 : -100));
    }
    assert(m.ranges(true, "None").notes.size() == 1 && !m.ranges(true, "None").vacuum);   // nothing with a part yet
    m.open = M::Stats::of(open);
    assert(!m.ranges(true, "Absolute").vacuum);
    m.baseline = M::Stats::of({ 50000 });
    m.level = M::Stats::of(held);
    std::vector<double> differences;
    for (double v : held) differences.push_back(v - 50000);
    m.difference = M::Stats::of(differences);
    M::Ranges on = m.ranges(true, "None");
    assert(on.method == "Absolute" && on.vacuum && !on.vacuum->stopped && on.notes.empty());
    assert(on.vacuum->low > 20000 && on.vacuum->low < 50000 && on.vacuum->high > 50000);
    assert(on.difference && on.difference->low < 0 && on.difference->high > 0);
    const M::Stats wide = M::Stats::of({ 44000, 56000, 47000, 53000 });
    const M::Range stopped = M::around(wide, m.open);
    const double boundary = stopped.low;
    assert(stopped.stopped && boundary > 20000 && boundary < wide.mean);
    assert(near((wide.mean - boundary) / wide.sigma(), (boundary - m.open->mean) / m.open->sigma()));
    // Too close: said, with the numbers.
    M close = m;
    close.open = M::Stats::of({ 49000, 49200, 48800 });
    M::Ranges near6 = close.ranges(true, "Difference");
    assert(near6.method == "Difference" && !near6.vacuum && !near6.difference && near6.notes.size() == 1
           && near6.notes[0].find("deviations apart") != std::string::npos);
    // Without a part not measured: said.
    M noOpen = m;
    noOpen.open.reset();
    assert(noOpen.ranges(true, "Absolute").notes.size() == 1);

    // Part off: Absolute from the level after the pulse; Difference from the level before it.
    M off;
    off.baseline = M::Stats::of({ 100, 102, 98 });
    off.level = M::Stats::of({ 5000, 5010, 4990 });
    off.difference = M::Stats::of({ 4900, 4908, 4892 });
    const M::Ranges abs = off.ranges(false, "Absolute"), diff = off.ranges(false, "Difference");
    assert(abs.vacuum && abs.vacuum->low < 5000 && abs.vacuum->high > 5000 && abs.notes.empty());
    assert(diff.vacuum && diff.vacuum->low < 100 && diff.vacuum->high > 100 && diff.difference->low < 4900);

    // Kept in the tip's file, and read back.
    JPNozzleTipConfig t;
    t.id = "T";
    t.partOn.measured = m;
    t.partOn.measured->when = "2026-10-15 19:00";
    const JPNozzleTipConfig back = JPNozzleTipConfig::fromJson(t.toJson());
    assert(back.partOn.measured && back.partOn.measured->when == "2026-10-15 19:00" && back.partOn.measured->open
           && near(back.partOn.measured->level.mean, m.level.mean) && back.partOn.measured->level.count == 20);
    assert(!back.partOff.measured);
    return 0;
}
