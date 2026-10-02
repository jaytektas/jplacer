// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPCellWatch.h"

#include "machine/JPCell.h"

#include <j/core/JButton.h>
#include <j/core/JContainer.h>
#include <j/core/JLabel.h>

#include <functional>
#include <string>
#include <utility>
#include <vector>

inline namespace jf {

// The machine as a whole: connected or not, on which port, homed or not,
// and what each controller says it is doing (Idle, Run, Alarm…). Connecting
// and homing are the toolbar's (JPConnectIcon, JPHomeIcon).
class JPMachinePanel : public JContainer {
public:
    JPMachinePanel(JSceneGraph& graph, JPCell& cell);

    // A serial controller's port was chosen (controller id, port path). The
    // owner stores it in the cell file; the panel only offers the choice.
    std::function<void(const std::string&, const std::string&)> onPortChosen;

private:
    void refresh(const std::string& why);
    // What the machine has been measured for: each camera's calibration, the
    // squareness, how it homes.
    void refreshCalibration();

    JPCell&     m_cell;
    JLabel*     m_status  = nullptr;
    JLabel*     m_state   = nullptr;
    std::vector<std::pair<std::string, JLabel*>> m_cameras;   // camera id, its line
    JLabel*     m_squareness = nullptr;
    JLabel*     m_homing  = nullptr;
    JPCellWatch m_watch;
};

} // inline namespace jf
