// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include <j/core/JControl.h>

#include <functional>
#include <map>
#include <string>
#include <utility>
#include <vector>

inline namespace jf {

// Where the chosen tool is, in the status bar: X, Y, Z and C. A click makes
// it relative, measured from where the tool is at that moment (a tape
// measure across the machine: jog to a second place and read the distance);
// another click makes it absolute again. Absolute in the theme's green,
// relative in its accent blue and saying so.
class JPPositionReadout : public JControl {
public:
    // The tool's coordinates now, by name ("X", "Y", "Z", "C"); none when
    // there is no tool.
    using Source = std::function<std::vector<std::pair<std::string, double>>()>;

    JPPositionReadout(JSceneGraph& graph, Source source);

    // How wide it needs to be for any coordinates a machine has.
    static float widthNeeded();
    bool isRelative() const { return m_relative; }

    void populateRenderPrimitives(JPrimitiveBuffer& buf) override;

private:
    void toggle();
    std::string text() const;

    Source                        m_source;
    bool                          m_relative = false;
    std::map<std::string, double> m_zero;   // where relative counts from
};

} // inline namespace jf
