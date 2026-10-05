// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// OpenPnP's Feeder.canTakeBackPart and takeBackPart, kind by kind: a tape
// or tray feeder once it has fed (the count taken back, but for a skipped
// feed); an auto feeder only when it says it recycles, and not twice; a
// push-pull feeder not twice; a loose part feeder where its part was found
// (then forgotten); a heap always; a drag feeder never.
// Tests check with assert(); a Release build must not compile it away.
#undef NDEBUG
#include <cassert>

#include "model/JPFeeder.h"

using namespace jf;

namespace {

JPFeeder make(const std::string& simple) {
    return JPFeeder::create("org.openpnp.machine.reference.feeder." + simple, "R1");
}

} // namespace

int main() {
    {
        JPFeeder strip = make("ReferenceStripFeeder");
        assert(!strip.canTakeBackPart());
        strip.setNumber("feed-count", 2);
        assert(strip.canTakeBackPart());
        strip.partTakenBack();
        assert(strip.number("feed-count") == 1);
        // A skipped feed's count is not taken back.
        strip.setFeedOptions(JPFeeder::FeedOptions::SkipNext);
        strip.partTakenBack();
        assert(strip.number("feed-count") == 1);
    }
    {
        JPFeeder rotated = make("ReferenceRotatedTrayFeeder");
        rotated.setNumber("feed-count", 1);
        rotated.setFeedOptions(JPFeeder::FeedOptions::SkipNext);
        rotated.partTakenBack();
        assert(rotated.number("feed-count") == 0 && !rotated.canTakeBackPart());
    }
    {
        JPFeeder autoFeeder = make("ReferenceAutoFeeder");
        assert(!autoFeeder.canTakeBackPart());
        autoFeeder.setFlag("recycle-support", true);
        assert(autoFeeder.canTakeBackPart());
        autoFeeder.partTakenBack();
        assert(autoFeeder.feedOptions() == JPFeeder::FeedOptions::SkipNext && !autoFeeder.canTakeBackPart());
    }
    {
        JPFeeder pushPull = make("ReferencePushPullFeeder");
        assert(pushPull.canTakeBackPart());
        pushPull.partTakenBack();
        assert(!pushPull.canTakeBackPart());
    }
    {
        JPFeeder loose = make("ReferenceLoosePartFeeder");
        assert(!loose.canTakeBackPart());
        loose.foundPick = JPLocation(JPLengthUnit::Millimeters, 10, 20, 0, 0);
        assert(loose.canTakeBackPart());
        loose.partTakenBack();
        assert(!loose.foundPick && !loose.canTakeBackPart());
    }
    assert(make("ReferenceHeapFeeder").canTakeBackPart());
    assert(!make("ReferenceDragFeeder").canTakeBackPart());
    return 0;
}
