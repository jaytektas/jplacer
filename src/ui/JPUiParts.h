// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include <j/core/JButton.h>
#include <j/core/JContainer.h>
#include <j/core/JLabel.h>

#include <functional>

#include <memory>
#include <string>

inline namespace jf {

class JMenu;
class JPIconButton;
class JPSearchField;
class JPTableTools;
class JPTable;

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
    // A table's Search at the end of a toolbar: its label, the line (JPSearchField, `columns` letters wide) and the
    // count beside it; the table attached to it once made (JPSearchField::attach).
    static JPSearchField* search(JSceneGraph& graph, JContainer& bar, int columns);
    // A table's tools (JPTableTools: Columns… and its views' buttons), after a divider, added to the end of a
    // toolbar's group; menus opened through the panel's `open` (its openMenu, read when clicked). Attached to
    // its table once that is made (JPTableTools::attach).
    static JPTableTools* tableTools(JSceneGraph& graph, JContainer& bar, const std::function<void(JMenu*, float x, float y)>* open);
};

} // inline namespace jf
