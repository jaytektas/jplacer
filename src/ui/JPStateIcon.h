// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include <j/core/JControl.h>
#include <j/graphics/VectorGraphics.h>

inline namespace jf {

// A toolbar icon that says the state of something by its ring: muted when
// idle, an amber three-quarter arc while busy, a green ring when good, a red
// ring with a break in it on a fault. The glyph inside says WHAT it is the
// state of; each icon draws its own (JPConnectIcon, JPHomeIcon). A click is
// the control's ordinary onClicked.
//
// The same idea as jayecu's connect chip, with every colour from the theme.
class JPStateIcon : public JControl {
public:
    enum class State { Idle, Busy, Good, Fault };

    JPStateIcon(JSceneGraph& graph, const char* name);

    void  setState(State s);
    State state() const { return m_state; }

    void populateRenderPrimitives(JPrimitiveBuffer& buf) override;

protected:
    // Draw the glyph centred at (cx, cy) inside an icon `size` across, in `ink`.
    virtual void drawGlyph(JVectorCanvas& vg, float cx, float cy, float size, const JColor& ink) const = 0;

private:
    State m_state = State::Idle;
};

} // inline namespace jf
