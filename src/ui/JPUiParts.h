// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include <j/core/JButton.h>
#include <j/core/JContainer.h>

#include <memory>
#include <string>

inline namespace jf {

// The small pieces every panel is built from, made one way.
class JPUiParts {
public:
    // A row of controls, as tall as its tallest kind of control, and never
    // squeezed shorter.
    static std::unique_ptr<JContainer> row(JSceneGraph& graph);
    // Make `c` a panel's column: padded, spaced, children stretched to its width.
    static void asPanel(JContainer& c);
    // A button as wide as its label: JButton's minimum width fits the text,
    // and a zero design width lets the layout settle on it.
    static std::unique_ptr<JButton> button(JSceneGraph& graph, const std::string& label);
    // A length (a coordinate) in millimetres as panels show it: in the System Units.
    static std::string coordinate(double v);
    // An angle in degrees as panels show it.
    static std::string angle(double degrees);
};

} // inline namespace jf
