// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPVerticalSlider.h"

#include <j/core/JStyle.h>
#include <j/core/JTextHelper.h>

#include <algorithm>
#include <cmath>

inline namespace jf {

namespace {

// The thumb's radius and the track's width, as shares of the style's slider height.
float thumbRadius() { return JStyle::current().sliderHeight * 0.4f; }
float trackWidth() { return JStyle::current().sliderHeight * 0.18f; }

} // namespace

JPVerticalSlider::JPVerticalSlider(JSceneGraph& graph, std::vector<std::pair<double, std::string>> marks, bool snap)
    : JControl(graph, "JPVerticalSlider"), m_marks(std::move(marks)), m_snap(snap) {}

float JPVerticalSlider::naturalWidth() const {
    const JStyle& st = JStyle::current();
    float widest = JTextHelper::measureWidth(m_caption);
    for (const auto& [at, label] : m_marks) widest = std::max(widest, 2 * thumbRadius() + st.spacing + JTextHelper::measureWidth(label));
    return std::ceil(widest) + st.spacing;
}

void JPVerticalSlider::setValue(double v) {
    v = std::clamp(v, 0.0, 1.0);
    if (m_snap && !m_marks.empty()) {
        // To the nearest mark.
        double best = m_marks.front().first;
        for (const auto& [at, label] : m_marks)
            if (std::abs(at - v) < std::abs(best - v)) best = at;
        v = best;
    }
    if (v == m_value) return;
    m_value = v;
    invalidate();
    onValueChanged.emit(v);
}

void JPVerticalSlider::setCaption(const std::string& text) {
    m_caption = text;
    invalidate();
}

void JPVerticalSlider::track(float& top, float& bottom) const {
    const JRect b = m_graph.getLayoutConst(m_nodeId).boundingBox;
    const float lh = JTextHelper::lineHeight();
    top = b.y + lh + JStyle::current().spacing + thumbRadius();
    bottom = b.y + b.height - std::max(thumbRadius(), lh * 0.5f);
}

void JPVerticalSlider::follow(float my) {
    float top = 0, bottom = 0;
    track(top, bottom);
    if (bottom <= top) return;
    setValue((bottom - my) / (bottom - top));
}

void JPVerticalSlider::handleMousePress(float mx, float my) {
    if (!isPointInside(mx, my)) return;
    setState(JWidgetState::Pressed);
    follow(my);
}

void JPVerticalSlider::handleMouseMove(float mx, float my) {
    JControl::handleMouseMove(mx, my);
    if (m_state == JWidgetState::Pressed) follow(my);
}

void JPVerticalSlider::populateRenderPrimitives(JPrimitiveBuffer& buf) {
    const JStyle& st = JStyle::current();
    const JRect b = m_graph.getLayoutConst(m_nodeId).boundingBox;
    float top = 0, bottom = 0;
    track(top, bottom);
    if (bottom <= top) return;
    const float r = thumbRadius(), tw = trackWidth(), cx = b.x + r;
    const auto y = [&](double at) { return float(bottom - at * (bottom - top)); };
    // The value above; the groove, filled up to the value; the marks and their labels.
    JTextHelper::pushText(buf, b.x, b.y, m_caption, st.LabelText);
    buf.pushRectangle(cx - tw * 0.5f, top, tw, bottom - top, st.Surface3, tw * 0.5f);
    buf.pushRectangle(cx - tw * 0.5f, y(m_value), tw, bottom - y(m_value), st.Accent, tw * 0.5f);
    const float lh = JTextHelper::lineHeight();
    for (const auto& [at, label] : m_marks) {
        buf.pushRectangle(cx + r * 0.6f, y(at) - st.borderWidth * 0.5f, r * 0.6f, st.borderWidth, st.Border);
        JTextHelper::pushText(buf, b.x + 2 * r + st.spacing, y(at) - lh * 0.5f, label, st.LabelText);
    }
    const uint8_t* thumb = m_state == JWidgetState::Pressed ? st.AccentPress : st.TextPrimary;
    buf.pushRectangle(cx - r, y(m_value) - r, 2 * r, 2 * r, thumb, r);
}

} // inline namespace jf
