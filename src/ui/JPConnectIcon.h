// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPStateIcon.h"

inline namespace jf {

// The machine's connection: a controller chip in the state ring. Idle when
// not connected, busy while connecting, good when connected, fault when the
// last connection failed or was lost.
class JPConnectIcon : public JPStateIcon {
public:
    explicit JPConnectIcon(JSceneGraph& graph) : JPStateIcon(graph, "JPConnectIcon") {}

protected:
    void drawGlyph(JVectorCanvas& vg, float cx, float cy, float size, const JColor& ink) const override {
        // A chip: a body and three pins either side (proportions of the icon).
        const float body = size * 0.30f, pin = size * 0.06f, pinW = size * 0.05f;
        vg.fillRoundedRect(cx - body / 2, cy - body / 2, body, body, size * 0.04f, JPaint::solid(ink));
        for (int i = -1; i <= 1; ++i) {
            const float y = cy + float(i) * body * 0.32f;
            vg.drawLine(cx - body / 2 - pin, y, cx - body / 2, y, pinW, JPaint::solid(ink));
            vg.drawLine(cx + body / 2, y, cx + body / 2 + pin, y, pinW, JPaint::solid(ink));
        }
    }
};

} // inline namespace jf
