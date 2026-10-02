// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "machine/JPCell.h"

#include <j/core/JButton.h>
#include <j/core/JComboBox.h>
#include <j/core/JContainer.h>
#include <j/core/JLabel.h>
#include <j/core/JLineEdit.h>
#include <j/core/JListView.h>

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

inline namespace jf {

// The running machine at a glance: whether it is connected, where every axis
// is, its actuators (switch them, read them), and a console to its
// controllers.
//
// Built for one cell; a different cell gets a new panel. Everything the cell
// reports arrives on its own threads and is re-posted to the main thread
// before it touches a widget.
class JPMachinePanel : public JContainer {
public:
    JPMachinePanel(JSceneGraph& graph, JPCell& cell);
    ~JPMachinePanel() override;

private:
    // Run `fn` on the main thread, unless this panel is gone by then.
    void onMain(std::function<void()> fn);

    void showConnection(bool connected, const std::string& why);
    void showPositions(const std::map<std::string, double>& positions);
    void showActuator(const std::string& id, bool ok, const std::string& value);
    void addConsoleLine(const std::string& line);
    void sendConsoleLine();

    JPCell&                         m_cell;
    JLabel*                         m_status  = nullptr;
    JButton*                        m_connect = nullptr;
    std::map<std::string, JLabel*>  m_axisValues;
    std::map<std::string, JLabel*>  m_actuatorValues;
    JListView*                      m_console    = nullptr;
    JComboBox*                      m_controller = nullptr;
    JLineEdit*                      m_input      = nullptr;
    std::vector<std::string>        m_consoleLines;

    std::vector<std::function<void()>> m_disconnects;   // from the cell's signals
    std::shared_ptr<bool>              m_alive = std::make_shared<bool>(true);
};

} // inline namespace jf
