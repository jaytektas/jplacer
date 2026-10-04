// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPBoardView.h"

#include <j/core/JStyle.h>
#include <j/core/JTextHelper.h>
#include <j/graphics/VectorGraphics.h>

#include <algorithm>
#include <limits>

inline namespace jf {

namespace {

JColor colour(const uint8_t c[4]) { return rgb(c[0], c[1], c[2]); }

} // namespace

JPBoardView::JPBoardView(JSceneGraph& graph) : JWidget(graph, "JPBoardView") {
    const JStyle& st = JStyle::current();
    setMinimumSize(6 * st.buttonHeight, 6 * st.buttonHeight);
}

void JPBoardView::show(const JPBoard& board, const JPBoardFrame& frame) {
    m_board = board;
    m_frame = frame;
    invalidate();
}

void JPBoardView::populateRenderPrimitives(JPrimitiveBuffer& buf) {
    const JRect b = bounds();
    const JStyle& st = JStyle::current();
    buf.pushRectangle(b.x, b.y, b.width, b.height, Colors::DockContentBg, 0.f);
    if (m_board.placements.empty()) {
        const std::string text = "No board read yet";
        JTextHelper::pushText(buf, b.x + std::max(0.f, (b.width - JTextHelper::measureWidth(text)) / 2),
                              b.y + (b.height - JTextHelper::lineHeight()) / 2, text, Colors::MutedText, b.width);
        return;
    }
    // What is drawn: the placements, the origin, and the outline.
    double x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    double px0 = std::numeric_limits<double>::max(), py0 = px0, px1 = -px0, py1 = -px0;
    for (const JPPlacement& p : m_board.placements) {
        px0 = std::min(px0, p.x);
        px1 = std::max(px1, p.x);
        py0 = std::min(py0, p.y);
        py1 = std::max(py1, p.y);
    }
    x0 = std::min(0.0, px0);
    y0 = std::min(0.0, py0);
    x1 = std::max({ 0.0, px1, m_frame.width });
    y1 = std::max({ 0.0, py1, m_frame.height });
    const float pad = 3 * st.spacing;
    const double scale = std::min((b.width - 2 * pad) / std::max(1.0, x1 - x0), (b.height - 2 * pad) / std::max(1.0, y1 - y0));
    const double ox = b.x + (b.width - (x1 - x0) * scale) / 2, oy = b.y + (b.height + (y1 - y0) * scale) / 2;
    auto sx = [&](double x) { return float(ox + (x - x0) * scale); };
    auto sy = [&](double y) { return float(oy - (y - y0) * scale); };

    JVectorCanvas vg;
    const float line = st.borderWidth;
    // The outline, or the placements' extent dashed in its place.
    if (m_frame.hasOutline()) {
        vg.strokeRect(sx(0), sy(m_frame.height), float(m_frame.width * scale), float(m_frame.height * scale), line,
                      JPaint::solid(colour(Colors::TextSecondary)));
    } else {
        vg.flush(buf);
        vg.clear();
        buf.pushDashedRect(sx(px0), sy(py1), float((px1 - px0) * scale), float((py1 - py0) * scale), Colors::MutedText, line);
    }
    // The origin: a cross.
    const float arm = 2 * st.spacing;
    vg.drawLine(sx(0) - arm, sy(0), sx(0) + arm, sy(0), line, JPaint::solid(colour(Colors::Danger)));
    vg.drawLine(sx(0), sy(0) - arm, sx(0), sy(0) + arm, line, JPaint::solid(colour(Colors::Danger)));
    // The placements.
    const float dot = std::max(line, st.spacing / 2);
    for (const JPPlacement& p : m_board.placements) {
        const float x = sx(p.x), y = sy(p.y);
        if (p.fiducial) vg.strokeCircle(x, y, 2 * dot, line, JPaint::solid(colour(Colors::Success)));
        else if (p.side == JPPlacement::Side::Bottom) vg.strokeCircle(x, y, dot, line, JPaint::solid(colour(Colors::Warning)));
        else vg.fillCircle(x, y, dot, JPaint::solid(colour(Colors::Accent)));
    }
    vg.flush(buf);
}

} // inline namespace jf
