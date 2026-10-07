// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// JPCameraFeed::knowDeviceControls: a feed made again for a camera (its panel remade after a setting changed)
// starts knowing what the feed before it learned of the device's settings (their ranges, defaults), so Device
// Settings keeps its Min, Max, Default and sliders while the camera is not on screen; it is only a start, not
// overwritten by a later one.
// Tests check with assert(); a Release build must not compile it away.
#undef NDEBUG
#include <cassert>

#include "camera/JPCameraFeed.h"

using namespace jf;

int main() {
    JPCameraConfig cam;
    cam.id = "B";
    cam.name = "Bottom";
    JPCameraFeed feed(cam);
    assert(feed.deviceControls().obj().empty());   // nothing known before it runs
    JJson known = JJson::object();
    known["brightness"]["min"] = -64;
    known["brightness"]["max"] = 64;
    known["brightness"]["default"] = 0;
    feed.knowDeviceControls(known);
    assert(feed.deviceControls()["brightness"]["max"].number() == 64);
    JJson other = JJson::object();
    other["brightness"]["max"] = 1;
    feed.knowDeviceControls(other);   // already known: kept
    assert(feed.deviceControls()["brightness"]["max"].number() == 64);
    return 0;
}
