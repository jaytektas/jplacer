// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include <j/core/JWidget.h>

#include <string>

inline namespace jf {

// A legend's line: a short stroke in a colour, then what it means
// ("——— Top outlines"), as OpenPnP's viewer explains its colours.
class JPColourKey : public JWidget {
public:
    // `colour`: one of the style's colours (Colors::…), kept by pointer so a
    // theme change is followed.
    JPColourKey(JSceneGraph& graph, const uint8_t* colour, std::string text);

    void populateRenderPrimitives(JPrimitiveBuffer& buf) override;

private:
    const uint8_t* m_colour;
    std::string    m_text;
};

} // inline namespace jf
