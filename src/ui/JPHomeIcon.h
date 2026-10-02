// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPStateIcon.h"

inline namespace jf {

// The machine's home: a house in the state ring. Idle when not homed, busy
// while homing, good when homed, fault when the last home failed.
class JPHomeIcon : public JPStateIcon {
public:
    explicit JPHomeIcon(JSceneGraph& graph) : JPStateIcon(graph, "JPHomeIcon") {}

protected:
    void drawGlyph(JVectorCanvas& vg, float cx, float cy, float size, const JColor& ink) const override {
        // A house: a roof and a body below it (proportions of the icon).
        const float half = size * 0.17f, roofTop = cy - size * 0.19f, eave = cy - size * 0.03f;
        vg.fillConvex({ { cx, roofTop }, { cx + half * 1.25f, eave }, { cx - half * 1.25f, eave } }, JPaint::solid(ink));
        vg.fillRect(cx - half * 0.8f, eave, half * 1.6f, size * 0.17f, JPaint::solid(ink));
    }
};

} // inline namespace jf
