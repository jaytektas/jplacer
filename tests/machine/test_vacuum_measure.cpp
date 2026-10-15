// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// A tip's part detection ranges from measured readings (JPVacuumMeasure), with readings like a bench machine's,
// whose sensor reads the pump's side of the valve (bigger: more vacuum). A range is told from the other state's
// level: its edge halfway between the two, its far edge as far again as the level
// itself (to 0, or twice it), so the pump's drift stays in it; two too close to tell apart set none. Part off: a level that comes back
// most of the way as the valve closes means the Valve close wait goes (0) and the check reads the level as the
// valve closes; a held part's level (part on, with a part) is a blocked nozzle's. Without the other state,
// kSigmas deviations either side, and said. A tip without a method is given Absolute. What was measured is kept
// in the tip's file.
// Tests check with assert(); a Release build must not compile it away.
#undef NDEBUG
#include <cassert>

#include "machine/JPNozzleTipConfig.h"
#include "machine/JPVacuumMeasure.h"

#include <cmath>

using namespace jf;

namespace {

bool near(double a, double b, double within = 1e-6) { return std::abs(a - b) < within; }

// `n` readings about `mean`, alternately `spread` either side.
std::vector<double> about(double mean, double spread, int n = 20) {
    std::vector<double> v;
    for (int i = 0; i < n; ++i) v.push_back(mean + (i % 2 ? spread : -spread));
    return v;
}

} // namespace

int main() {
    using M = JPVacuumMeasure;

    // Readings 10, 12, 14: mean 12, deviation 2, resolution 2; with nothing to tell them from, 6 deviations either side.
    const M::Stats s = M::Stats::of({ 10, 12, 14 });
    assert(s.count == 3 && near(s.mean, 12) && near(s.sd, 2) && s.lowest == 10 && s.highest == 14 && s.resolution == 2);
    const auto r = M::around(s, std::nullopt);
    assert(r && near(r->low, 0) && near(r->high, 24) && !r->bounded);
    // All the same whole number: resolution 1, so half a unit of deviation (never a range of nothing).
    const M::Stats same = M::Stats::of({ -31000, -31000, -31000 });
    assert(same.sd == 0 && same.resolution == 1 && near(same.sigma(), 0.5) && near(same.halfWidth(), 3));
    // Told from another level: the edge halfway between them (whatever their deviations), the far edge as far again as
    // the level; the other way round, down to 0.
    const M::Stats held = M::Stats::of(about(72000, 300)), open = M::Stats::of(about(61000, 300));
    const auto toldOn = M::around(held, open), toldOff = M::around(open, held);
    assert(toldOn && toldOn->bounded && near(toldOn->low, 66500, 1) && near(toldOn->high, 144000, 1));
    assert(toldOff && near(toldOff->low, 0, 1) && near(toldOff->high, 66500, 1));
    // Halfway, however unlike their deviations: a steady level beside a noisy one does not pull the line to itself
    // (a pump's cycle moves the steady one further than a few seconds show).
    const auto steady = M::around(M::Stats::of(about(88000, 20)), M::Stats::of(about(30000, 3000)));
    assert(steady && near(steady->low, 59000, 1));
    // Too close: none.
    assert(!M::around(held, M::Stats::of(about(71000, 300))));

    // Part on, with a part about 72000, without about 61000: the range from the boundary up; the difference while
    // held told from the drop a lost part would make.
    M on;
    assert(on.ranges(true, "None", 0, nullptr).notes.size() == 1 && !on.ranges(true, "None", 0, nullptr).vacuum);
    on.take({ {}, about(61000, 300), {} }, true, false);
    assert(on.open && !on.held() && !on.ranges(true, "Absolute", 0, nullptr).vacuum);
    on.take({ { 72000 }, about(71800, 300), {} }, true, true);
    assert(on.held() && on.open && near(on.difference.mean, -200, 1));
    const M::Ranges onR = on.ranges(true, "None", 0, nullptr);
    assert(onR.method == "Absolute" && onR.vacuum && onR.vacuum->bounded && onR.notes.empty());
    assert(near(onR.vacuum->low, 66400, 1) && near(onR.vacuum->high, 143600, 1));   // the pump may drift up
    assert(onR.difference && onR.difference->low < -200 && onR.difference->low > -10800 && onR.difference->high > 0);
    // Too close: said, nothing set.
    M close = on;
    close.open = M::Stats::of(about(71500, 200));
    const M::Ranges closeR = close.ranges(true, "Difference", 0, nullptr);
    assert(closeR.method == "Difference" && !closeR.vacuum && !closeR.difference && closeR.notes.size() == 1
           && closeR.notes[0].find("deviations apart") != std::string::npos);
    // Without a part not measured: said, the range its own readings' spread.
    M noOpen = on;
    noOpen.open.reset();
    const M::Ranges noOpenR = noOpen.ranges(true, "Absolute", 0, nullptr);
    assert(noOpenR.vacuum && !noOpenR.vacuum->bounded && noOpenR.notes.size() == 1);

    // Part off on the bench: before each pulse about 86900, as the valve closes about 65000 (an open nozzle), after
    // a 250 ms wait back to 86700: the wait reads the pump, so it goes, and the check reads as the valve closes.
    M off;
    off.take({ about(86900, 300), about(65000, 150), about(86700, 300) }, false, false);
    assert(off.settled && off.settledDifference && near(off.difference.mean, -21900, 1));
    const M::Ranges alone = off.ranges(false, "Absolute", 250, nullptr);
    assert(alone.dwellMs && *alone.dwellMs == 0 && alone.vacuum && !alone.vacuum->bounded && alone.notes.size() == 2);
    // Told from a blocked nozzle's level (Part On with a part): from the boundary down; a nozzle against the object
    // (68660 as it began to close off, 78265 sealed) is out of it, one in the air (65839) in it.
    const M::Ranges offR = off.ranges(false, "Absolute", 250, &on);
    assert(offR.dwellMs && *offR.dwellMs == 0 && offR.vacuum && offR.vacuum->bounded && offR.notes.size() == 1);
    assert(offR.vacuum->high > 65839 && offR.vacuum->high < 68660 && near(offR.vacuum->low, 0, 1));
    // Difference: the level before the pulse told from the pump pulled down by an open nozzle (part on, without a
    // part), wide enough for its drift (79000 to 88000 on the bench); the change from a blocked nozzle's.
    const M::Ranges diffR = off.ranges(false, "Difference", 0, &on);
    assert(diffR.vacuum && diffR.vacuum->low < 79000 && diffR.vacuum->high > 88000 && !diffR.dwellMs);
    assert(diffR.difference && diffR.difference->high > -21900 && diffR.difference->high < -14900);
    // LEFT on the bench at 20:11, the pump since up from 79000 to 88000: the pulse's change against a held part's
    // drop is only some 4 deviations apart (the two measured at different pump levels), its level 11: Difference
    // cannot tell them apart, Absolute can, and is set; the wait goes either way.
    M left;
    left.take({ about(87992, 900), about(65303, 293), about(87778, 887) }, false, false);
    const M::Ranges switched = left.ranges(false, "Difference", 250, &on);
    assert(switched.method == "Absolute" && switched.vacuum && switched.vacuum->bounded && !switched.tooClose);
    assert(switched.dwellMs && *switched.dwellMs == 0);
    bool said = false;
    for (const std::string& n : switched.notes) said = said || n.find("Measurement Method set to Absolute") != std::string::npos;
    assert(said);
    // Neither telling them apart: nothing set, said, the wait still gone.
    M neither = left;
    neither.level = M::Stats::of(about(71000, 300));
    neither.difference = M::Stats::of(about(-16900, 600));
    const M::Ranges none = neither.ranges(false, "Difference", 250, &on);
    assert(none.tooClose && !none.vacuum && none.dwellMs && *none.dwellMs == 0);
    // A sensor on the nozzle's side: the level stays low through the wait, which is kept and read.
    M nozzleSide;
    nozzleSide.take({ about(86900, 300), about(65000, 150), about(20000, 300) }, false, false);
    const M::Ranges keep = nozzleSide.ranges(false, "Absolute", 250, &on);
    assert(!keep.dwellMs && keep.vacuum && !keep.vacuum->bounded && near((keep.vacuum->low + keep.vacuum->high) / 2, 20000, 1));
    // Measured with no wait, the wait since set: measure again.
    M noWait;
    noWait.take({ about(86900, 300), about(65000, 150), {} }, false, false);
    assert(!noWait.ranges(false, "Absolute", 250, &on).vacuum && noWait.ranges(false, "Absolute", 0, &on).vacuum);

    // Kept in the tip's file, and read back.
    JPNozzleTipConfig t;
    t.id = "T";
    t.partOn.measured = on;
    t.partOn.measured->when = "2026-10-15 19:00";
    t.partOff.measured = off;
    const JPNozzleTipConfig back = JPNozzleTipConfig::fromJson(t.toJson());
    assert(back.partOn.measured && back.partOn.measured->when == "2026-10-15 19:00" && back.partOn.measured->open
           && near(back.partOn.measured->level.mean, on.level.mean) && back.partOn.measured->level.count == 20);
    assert(back.partOff.measured && back.partOff.measured->settled && near(back.partOff.measured->settled->mean, 86700, 1e-6));
    return 0;
}
