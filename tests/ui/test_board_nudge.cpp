// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// Board Nudge (JPBoardNudge): begun where the camera is, what it is jogged since given to Apply (in millimetres,
// the camera in inches too), and the instructions shown and taken away; Cancel gives nothing; with no machine it
// does not begin, and the machine gone ends it.
// Tests check with assert(); a Release build must not compile it away.
#undef NDEBUG
#include <cassert>

#include "ui/JPBoardNudge.h"

#include <cmath>

using namespace jf;

int main() {
    std::optional<JPLocation> camera = JPLocation(JPLengthUnit::Millimeters, 10, 20, 0, 0);
    int shown = 0, hidden = 0, applied = 0;
    bool lit = false;
    double dx = 0, dy = 0;
    std::string text;
    JPBoardNudge nudge({ [&] { return camera; },
                         [&](const std::string& t) {
                             ++shown;
                             text = t;
                         },
                         [&] { ++hidden; },
                         [&](double x, double y) {
                             ++applied;
                             dx = x;
                             dy = y;
                         },
                         [&](bool on) { lit = on; } });
    std::string why;
    assert(nudge.start(why) && nudge.active() && lit && shown == 1 && text == JPBoardNudge::text(0, 0));
    // Jogged: what it moved by, applied to the board.
    camera = JPLocation(JPLengthUnit::Millimeters, 10.03, 19.98, 0, 0);
    nudge.apply();
    assert(applied == 1 && std::abs(dx - 0.03) < 1e-9 && std::abs(dy + 0.02) < 1e-9);
    assert(!nudge.active() && !lit && hidden == 1);
    // Cancelled: nothing applied.
    assert(nudge.start(why));
    camera = JPLocation(JPLengthUnit::Millimeters, 11, 21, 0, 0);
    nudge.cancel();
    assert(applied == 1 && !nudge.active() && hidden == 2);
    // The camera's position in inches: the jog in millimetres.
    camera = JPLocation(JPLengthUnit::Inches, 1, 1, 0, 0);
    assert(nudge.start(why));
    camera = JPLocation(JPLengthUnit::Inches, 1.001, 1, 0, 0);
    nudge.apply();
    assert(applied == 2 && std::abs(dx - 0.0254) < 1e-9 && std::abs(dy) < 1e-9);
    // Not jogged: nothing to apply.
    assert(nudge.start(why));
    nudge.apply();
    assert(applied == 2 && !nudge.active());
    // No machine: not begun; the machine gone as it is applied: nothing applied.
    camera.reset();
    assert(!nudge.start(why) && !why.empty() && !nudge.active());
    camera = JPLocation(JPLengthUnit::Millimeters, 0, 0, 0, 0);
    assert(nudge.start(why));
    camera.reset();
    nudge.apply();
    assert(applied == 2 && !nudge.active() && !lit);
    return 0;
}
