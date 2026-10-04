// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPFootprintView.h"

#include "JPFootprintDraw.h"

#include <j/core/JStyle.h>

#include <algorithm>
#include <cmath>

inline namespace jf {

namespace {

JColor colour(const uint8_t c[4]) { return rgb(c[0], c[1], c[2]); }

} // namespace

JPFootprintView::JPFootprintView(JSceneGraph& graph) : JWidget(graph, "JPFootprintView") {
    const JStyle& st = JStyle::current();
    setMinimumSize(4 * st.buttonHeight, 4 * st.buttonHeight);
}

void JPFootprintView::show(std::optional<JPFootprint> footprint) {
    m_footprint = std::move(footprint);
    invalidate();
}

void JPFootprintView::populateRenderPrimitives(JPrimitiveBuffer& buf) {
    const JRect b = bounds();
    const JStyle& st = JStyle::current();
    buf.pushRectangle(b.x, b.y, b.width, b.height, Colors::DockContentBg, 0.f);
    if (!m_footprint || m_footprint->pads.empty()) return;
    // What it covers: its pads and its body.
    double x0 = -m_footprint->bodyWidth / 2, x1 = -x0, y0 = -m_footprint->bodyLength / 2, y1 = -y0;
    for (const JPPad& p : m_footprint->pads) {
        const double r = std::hypot(p.width, p.height) / 2;
        x0 = std::min(x0, p.x - r);
        x1 = std::max(x1, p.x + r);
        y0 = std::min(y0, p.y - r);
        y1 = std::max(y1, p.y + r);
    }
    const float pad = 2 * st.spacing;
    const double scale = std::min((b.width - 2 * pad) / std::max(1e-6, x1 - x0), (b.height - 2 * pad) / std::max(1e-6, y1 - y0));
    const double cx = b.x + b.width / 2, cy = b.y + b.height / 2, mx = (x0 + x1) / 2, my = (y0 + y1) / 2;
    const JPFootprintDraw::Place place = [&](double x, double y, float& sx, float& sy) {
        sx = float(cx + (x - mx) * scale);
        sy = float(cy - (y - my) * scale);   // Y up
        return true;
    };
    JVectorCanvas vg;
    JPFootprintDraw::draw(vg, *m_footprint, place, st.borderWidth,
                          { colour(Colors::Accent), colour(Colors::Warning), colour(Colors::MutedText) });
    vg.flush(buf);
}

} // inline namespace jf
