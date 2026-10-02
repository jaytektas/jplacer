// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPCellWatch.h"

#include "machine/JPCell.h"

#include <j/core/JContainer.h>
#include <j/core/JLabel.h>

#include <map>
#include <string>

inline namespace jf {

// The cell's actuators: On / Off for what can be switched, Read for what
// can be read, and beside each the last result or why it failed. Scrolls,
// since a machine has as many as its configuration says.
class JPActuatorPanel : public JContainer {
public:
    JPActuatorPanel(JSceneGraph& graph, JPCell& cell);

private:
    JPCell&                        m_cell;
    std::map<std::string, JLabel*> m_values;
    JPCellWatch                    m_watch;
};

} // inline namespace jf
