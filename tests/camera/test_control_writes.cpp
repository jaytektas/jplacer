// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// A camera's settings sent only when they change (JPControlWrites): a value sent once is not sent again until it
// changes; one forgotten (its automatic mode changed, or setting it failed) is sent again; cleared (the camera
// opened again, or set afresh), everything is.
// Tests check with assert(); a Release build must not compile it away.
#undef NDEBUG
#include <cassert>

#include "camera/JPControlWrites.h"

using namespace jf;

int main() {
    JPControlWrites w;
    const uint32_t exposure = 1, autoExposure = 2;
    assert(w.needed(exposure, 500));
    w.sent(exposure, 500);
    assert(!w.needed(exposure, 500) && w.needed(exposure, 501));
    w.sent(exposure, 501);
    assert(!w.needed(exposure, 501));
    // Its automatic mode changed: the camera may have moved it, so it is sent again.
    w.sent(autoExposure, 1);
    w.forget(exposure);
    assert(w.needed(exposure, 501) && !w.needed(autoExposure, 1));
    w.sent(exposure, 501);
    // Opened again: everything sent once more.
    w.clear();
    assert(w.needed(exposure, 501) && w.needed(autoExposure, 1));
    return 0;
}
