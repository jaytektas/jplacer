// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPSearchStrip.h"

#include <j/core/JStyle.h>
#include <j/graphics/RenderPrimitive.h>

inline namespace jf {

void JPSearchStrip::setStates(std::vector<int> states) {
    if (states == m_states) return;
    m_states = std::move(states);
    invalidate();
}

void JPSearchStrip::populateRenderPrimitives(JPrimitiveBuffer& buf) {
    const JRect b = bounds();
    const JStyle& st = JStyle::current();
    const size_t n = m_states.size();
    for (size_t i = 0; i < n; ++i) {
        const float x0 = b.x + b.width * float(i) / float(n), x1 = b.x + b.width * float(i + 1) / float(n);
        const auto& colour = m_states[i] == Searching ? Colors::Warning
                           : m_states[i] == Found     ? Colors::Success
                           : m_states[i] == Missing   ? Colors::Accent
                                                      : Colors::MutedText;
        buf.pushRectangle(x0, b.y, x1 - x0, b.height, colour, 0.f);
    }
    buf.pushRectangle(b.x, b.y, b.width, b.height, Colors::Transparent, 0.f, st.borderWidth, Colors::Border);
}

} // inline namespace jf
