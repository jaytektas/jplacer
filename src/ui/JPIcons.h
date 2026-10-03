// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include <j/graphics/VectorGraphics.h>

inline namespace jf {

// jplacer's icons, each drawn centred at (cx, cy) to fit `size` across, in
// `ink` (JPIconButton::Glyph). Shapes only: proportions of the size, colours
// from the caller (the theme).
class JPIcons {
public:
    // An eye: looking at something as it is (a picture as taken).
    static void eye(JVectorCanvas& vg, float cx, float cy, float size, const JColor& ink);
    // A floppy disk: save.
    static void save(JVectorCanvas& vg, float cx, float cy, float size, const JColor& ink);
    // A target: a ring with a centre and four ticks (calibrate).
    static void target(JVectorCanvas& vg, float cx, float cy, float size, const JColor& ink);
    // A tick in a ring: check that something is right (a test).
    static void check(JVectorCanvas& vg, float cx, float cy, float size, const JColor& ink);
};

} // inline namespace jf
