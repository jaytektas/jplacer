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
    // A plus / a minus in a box: open everything / close everything (a tree).
    static void expandAll(JVectorCanvas& vg, float cx, float cy, float size, const JColor& ink);
    static void collapseAll(JVectorCanvas& vg, float cx, float cy, float size, const JColor& ink);
    // A gear: settings.
    static void gear(JVectorCanvas& vg, float cx, float cy, float size, const JColor& ink);
    // Take a place from where the camera is (a viewfinder round a ring), or
    // from where the nozzle is (a nozzle over a ring).
    static void captureCamera(JVectorCanvas& vg, float cx, float cy, float size, const JColor& ink);
    static void captureNozzle(JVectorCanvas& vg, float cx, float cy, float size, const JColor& ink);
    // Go to a place with the camera / the nozzle (an arrow to it).
    static void moveCamera(JVectorCanvas& vg, float cx, float cy, float size, const JColor& ink);
    static void moveNozzle(JVectorCanvas& vg, float cx, float cy, float size, const JColor& ink);
    // Jogging: an arrow each way; a turn anticlockwise and clockwise; a house (home).
    static void arrowUp(JVectorCanvas& vg, float cx, float cy, float size, const JColor& ink);
    static void arrowDown(JVectorCanvas& vg, float cx, float cy, float size, const JColor& ink);
    static void arrowLeft(JVectorCanvas& vg, float cx, float cy, float size, const JColor& ink);
    static void arrowRight(JVectorCanvas& vg, float cx, float cy, float size, const JColor& ink);
    static void rotateAnticlockwise(JVectorCanvas& vg, float cx, float cy, float size, const JColor& ink);
    static void rotateClockwise(JVectorCanvas& vg, float cx, float cy, float size, const JColor& ink);
    static void home(JVectorCanvas& vg, float cx, float cy, float size, const JColor& ink);
};

} // inline namespace jf
