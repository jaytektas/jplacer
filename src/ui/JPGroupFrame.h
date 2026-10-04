// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include <j/core/JContainer.h>

#include <string>

inline namespace jf {

// A titled group of controls: a thin frame round them with the title on its
// top edge, as a settings page groups what belongs together ("Coordinate
// System", "Safe Z"). Its children are a column inside the frame.
class JPGroupFrame : public JContainer {
public:
    JPGroupFrame(JSceneGraph& graph, std::string title);

    // How much taller and wider the frame is than what it holds.
    static float extraHeight();
    static float extraWidth();

    void setTitle(std::string title) { m_title = std::move(title); invalidate(); }

    void populateRenderPrimitives(JPrimitiveBuffer& buf) override;

private:
    std::string m_title;
};

} // inline namespace jf
