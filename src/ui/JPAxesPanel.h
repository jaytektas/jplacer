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

// Every axis in the cell and where it is, live: controller axes, the ones
// that follow another through a map, and the virtual ones. For setting up
// and diagnosing a machine; moving it is the Jog panel's.
class JPAxesPanel : public JContainer {
public:
    JPAxesPanel(JSceneGraph& graph, JPCell& cell);

private:
    std::map<std::string, JLabel*> m_values;
    JPCellWatch                    m_watch;
};

} // inline namespace jf
