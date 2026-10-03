// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include <j/core/JControl.h>
#include <j/graphics/VectorGraphics.h>

#include <functional>
#include <string>

inline namespace jf {

// A small square button showing an icon (a glyph drawn to its size, see
// JPIcons) and saying what it does in its tooltip: for a panel's tools in
// its tab. Flat at rest, lit on hover and press. A checkable one stays on
// the accent while on (onToggled says which).
class JPIconButton : public JControl {
public:
    using Glyph = std::function<void(JVectorCanvas& vg, float cx, float cy, float size, const JColor& ink)>;

    JPIconButton(JSceneGraph& graph, const std::string& name, Glyph glyph, const std::string& tooltip);

    // How big one is: as tall as a tab bar, and as wide.
    static float size();

    void setCheckable(bool on) { m_checkable = on; }
    void setChecked(bool on);
    bool isChecked() const { return m_checked; }
    jf::JSignal<bool> onToggled;   // a checkable one clicked: on or off now

    void populateRenderPrimitives(JPrimitiveBuffer& buf) override;

private:
    Glyph m_glyph;
    bool  m_checkable = false;
    bool  m_checked = false;
};

} // inline namespace jf
