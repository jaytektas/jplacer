// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// OpenPnP's nozzle tip template cloning: a tip takes the template's changer
// steps, each move moved by how far its own first move is from the
// template's (a coordinate left out stays left out); a locked tip, or one
// without a first move to go by, is left as it is.
// Tests check with assert(); a Release build must not compile it away.
#undef NDEBUG
#include <cassert>

#include "machine/JPNozzleTipConfig.h"

using namespace jf;

namespace {

JPChangerStep moveTo(std::optional<double> x, std::optional<double> y, std::optional<double> z) {
    JPChangerStep s;
    s.kind = JPChangerStep::Kind::Move;
    s.x = x;
    s.y = y;
    s.z = z;
    return s;
}

} // namespace

int main() {
    JPNozzleTipConfig templ;
    templ.id = "T1";
    templ.templateTip = true;
    templ.loadSteps = { moveTo(100, 50, -5), moveTo(std::nullopt, 60, -10), moveTo(100, 60, std::nullopt) };
    JPChangerStep wait;
    wait.kind = JPChangerStep::Kind::Wait;
    wait.waitMs = 200;
    templ.loadSteps.insert(templ.loadSteps.begin() + 1, wait);
    templ.unloadReversesLoad = false;
    templ.unloadSteps = { moveTo(100, 60, -10), moveTo(100, 50, -5) };

    JPNozzleTipConfig clone;
    clone.id = "T2";
    clone.loadSteps = { moveTo(120, 50, -6) };   // its slot: 20 mm along X, 1 mm lower
    assert(clone.cloneChangerFrom(templ));
    assert(clone.loadSteps.size() == 4 && clone.loadSteps[1].kind == JPChangerStep::Kind::Wait);
    assert(*clone.loadSteps[0].x == 120 && *clone.loadSteps[0].y == 50 && *clone.loadSteps[0].z == -6);
    assert(!clone.loadSteps[2].x && *clone.loadSteps[2].y == 60 && *clone.loadSteps[2].z == -11);
    assert(*clone.loadSteps[3].x == 120 && !clone.loadSteps[3].z);
    assert(!clone.unloadReversesLoad && *clone.unloadSteps[1].x == 120 && *clone.unloadSteps[0].z == -11);

    // Locked: untouched.
    JPNozzleTipConfig locked = clone;
    locked.templateLocked = true;
    locked.loadSteps = { moveTo(140, 50, -5) };
    assert(!locked.cloneChangerFrom(templ) && locked.loadSteps.size() == 1);
    // Nothing to go by: untouched.
    JPNozzleTipConfig bare;
    assert(!bare.cloneChangerFrom(templ) && bare.loadSteps.empty());
    return 0;
}
