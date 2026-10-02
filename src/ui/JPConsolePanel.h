// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPCellWatch.h"

#include "machine/JPCell.h"

#include <j/core/JComboBox.h>
#include <j/core/JContainer.h>
#include <j/core/JLineEdit.h>
#include <j/core/JListView.h>

#include <string>
#include <vector>

inline namespace jf {

// What passes between jplacer and the controllers, newest at the top (status
// reports left out), and a line to send to one of them as typed.
class JPConsolePanel : public JContainer {
public:
    JPConsolePanel(JSceneGraph& graph, JPCell& cell);

private:
    void addLine(const std::string& line);
    void send();

    JPCell&                  m_cell;
    JListView*               m_list       = nullptr;
    JComboBox*               m_controller = nullptr;
    JLineEdit*               m_input      = nullptr;
    std::vector<std::string> m_lines;
    JPCellWatch              m_watch;
};

} // inline namespace jf
