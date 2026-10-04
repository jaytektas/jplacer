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
//
// What a click does is never left to guess: one that opens a menu shows a
// small down-triangle in its bottom-right corner, one that leads somewhere
// else (another window, a dialog, Machine Setup) shows "…" there, as a text
// button or menu entry ends in "…"; one without either acts at once.
class JPIconButton : public JControl {
public:
    using Glyph = std::function<void(JVectorCanvas& vg, float cx, float cy, float size, const JColor& ink)>;

    JPIconButton(JSceneGraph& graph, const std::string& name, Glyph glyph, const std::string& tooltip);
    // One showing an OpenPnP icon by its name (JPOpenPnpIcons), as OpenPnP's
    // buttons do.
    JPIconButton(JSceneGraph& graph, const std::string& name, const std::string& openPnpIcon, const std::string& tooltip);

    // How big one is: as tall as a tab bar, and as wide.
    static float size();

    // Another OpenPnP icon (a button that changes with what it does: Start
    // and Pause, Defer and Alert Errors).
    void setIcon(const std::string& openPnpIcon) {
        m_icon = openPnpIcon;
        m_graph.invalidateNode(m_nodeId, DirtySelf);
    }
    void setCheckable(bool on) { m_checkable = on; }
    // Drawn as a button at rest too (a surface and an edge), for a pad of
    // controls rather than a panel's tools.
    void setFramed(bool on) { m_framed = on; }
    enum class Leads { Nowhere, Menu, Elsewhere };
    void setLeads(Leads leads) { m_leads = leads; }
    void setChecked(bool on);
    bool isChecked() const { return m_checked; }
    jf::JSignal<bool> onToggled;   // a checkable one clicked: on or off now

    void populateRenderPrimitives(JPrimitiveBuffer& buf) override;

private:
    Glyph       m_glyph;
    std::string m_icon;   // an OpenPnP icon's name, instead of a glyph
    bool  m_checkable = false;
    bool  m_framed    = false;
    bool  m_checked = false;
    Leads m_leads   = Leads::Nowhere;
};

} // inline namespace jf
