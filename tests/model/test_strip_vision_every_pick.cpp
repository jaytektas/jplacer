// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// A strip feeder's vision on a tape of 2 mm part pitch (an 0402's), where a sprocket hole serves two parts
// (JPFeeder::visionExpected): with no Extrapolation Distance the hole is looked at for each of its two parts, so
// vision runs every pick; with one set, the second part of a pair goes by the first's look, as OpenPnP does; on
// 4 mm pitch, each pick looks at its own hole; the holes set again, what vision found forgotten.
// Tests check with assert(); a Release build must not compile it away.
#undef NDEBUG
#include <cassert>

#include "model/JPFeeder.h"
#include "model/JPStripFeeder.h"

#include <cmath>

using namespace jf;

namespace {

const JPLengthUnit kMm = JPLengthUnit::Millimeters;

JPFeeder strip(double pitchMm, double extrapolationMm) {
    JPFeeder f = JPFeeder::create(JPStripFeeder::kClass, "R1");
    JPStripFeeder* s = f.as<JPStripFeeder>();
    s->referenceHole = JPLocation(kMm, 10, 10, 0, 0);
    s->lastHole = JPLocation(kMm, 50, 10, 0, 0);
    s->partPitch = JPLength(pitchMm, kMm);
    s->visionEnabled = true;
    s->extrapolationDistance = JPLength(extrapolationMm, kMm);
    return f;
}

} // namespace

int main() {
    // 2 mm pitch, no extrapolation: parts 1 and 2 share the first hole, and both are looked at.
    JPFeeder f = strip(2, 0);
    auto first = f.visionExpected(1);
    assert(first);
    f.setVisionFound(1, *first);
    assert(f.visionExpected(2));   // the same hole, looked at again for the second part
    // An Extrapolation Distance: the second part of the pair goes by the first's look.
    JPFeeder g = strip(2, 12);
    first = g.visionExpected(1);
    assert(first);
    g.setVisionFound(1, *first);
    assert(!g.visionExpected(2));
    // 4 mm pitch: each part its own hole, each looked at.
    JPFeeder h = strip(4, 0);
    first = h.visionExpected(1);
    h.setVisionFound(1, *first);
    assert(h.visionExpected(2));
    // Vision off: nothing looked at.
    JPFeeder off = strip(2, 0);
    off.as<JPStripFeeder>()->visionEnabled = false;
    assert(!off.visionExpected(1));
    // The holes set again (Auto Setup, or typed): what vision found from the old ones forgotten, so the first part
    // is picked where the new holes put it, as Reset would.
    JPFeeder moved = strip(4, 0);
    first = moved.visionExpected(1);
    moved.setVisionFound(1, first->add(JPLocation(kMm, 0.8, -0.6, 0, 0)));
    moved.setVisionFound(3, *moved.visionExpected(3));
    moved.setStripHole(1, JPLocation(kMm, 12, 20, 0, 0));
    moved.setStripHole(2, JPLocation(kMm, 52, 20, 0, 0));
    assert(!moved.visionLocation && !moved.visionLocationReference);
    moved.setFeedCount(1);
    JPFeeder fresh = strip(4, 0);
    fresh.setStripHole(1, JPLocation(kMm, 12, 20, 0, 0));
    fresh.setStripHole(2, JPLocation(kMm, 52, 20, 0, 0));
    fresh.setFeedCount(1);
    const JPLocation a = moved.pickLocation()->convertToUnits(kMm), b = fresh.pickLocation()->convertToUnits(kMm);
    assert(std::abs(a.x() - b.x()) < 1e-9 && std::abs(a.y() - b.y()) < 1e-9);
    return 0;
}
