// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// A strip feeder's vision on a tape of 2 mm part pitch (an 0402's), where a sprocket hole serves two parts
// (JPFeeder::visionExpected): with no Extrapolation Distance the hole is looked at for each of its two parts, so
// vision runs every pick; with one set, the second part of a pair goes by the first's look, as OpenPnP does; on
// 4 mm pitch, each pick looks at its own hole.
// Tests check with assert(); a Release build must not compile it away.
#undef NDEBUG
#include <cassert>

#include "model/JPFeeder.h"
#include "model/JPStripFeeder.h"

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
    return 0;
}
