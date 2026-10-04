// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include <j/core/JButton.h>

#include <string>

inline namespace jf {

// A text button that opens a menu: its label, then a small down-triangle at
// its right saying so (as an icon button that opens one shows it in its
// corner, JPIconButton::Leads::Menu). The owner opens the menu on onClicked.
class JPMenuButton : public JButton {
public:
    JPMenuButton(JSceneGraph& graph, const std::string& label);

    void populateRenderPrimitives(JPrimitiveBuffer& buf) override;
};

} // inline namespace jf
