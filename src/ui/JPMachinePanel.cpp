// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPMachinePanel.h"

#include "machine/JPSerialPorts.h"

#include <j/core/JStyle.h>
#include <j/core/MainThreadDispatcher.h>

#include <cstdio>

inline namespace jf {

namespace {

// How much console history is kept: enough to scroll back through a homing or
// a settings dump, bounded so a long session does not grow without end.
constexpr size_t kConsoleLines = 1000;

std::string formatCoordinate(double v) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.3f", v);
    return buf;
}

std::unique_ptr<JContainer> row(JSceneGraph& graph) {
    auto c = std::make_unique<JContainer>(graph);
    c->setDirection(JFlexDirection::JRow)->setGap(JStyle::current().spacing)->setAlignItems(JAlignItems::Center);
    return c;
}

// A button as wide as its label: JButton's minimum width fits the text, and a
// zero design width lets the layout settle on it.
std::unique_ptr<JButton> button(JSceneGraph& graph, const char* label) {
    return std::make_unique<JButton>(graph, label, 0.f);
}

std::unique_ptr<JContainer> form(JSceneGraph& graph) {
    auto c = std::make_unique<JContainer>(graph);
    c->setLayoutMode(JLayoutMode::Form)->setGap(JStyle::current().spacing);
    return c;
}

} // namespace

JPMachinePanel::JPMachinePanel(JSceneGraph& graph, JPCell& cell)
    : JContainer(graph), m_cell(cell) {
    const JStyle& st = JStyle::current();
    setDirection(JFlexDirection::Column)->setGap(2 * st.spacing)->setPadding(JEdges{ st.fieldPadding })
        ->setAlignItems(JAlignItems::Stretch);

    // Connection.
    auto top = row(graph);
    m_status = top->add(std::make_unique<JLabel>(graph, cell.config().name));
    m_status->setHSizePolicy(JSizePolicyMode::Expanding, 1);
    m_connect = top->add(std::make_unique<JButton>(graph, "Connect"));
    m_connect->onClicked.connect([this] {
        if (m_cell.isConnected()) m_cell.disconnect();
        else {
            m_status->setText("Connecting\xE2\x80\xA6");
            m_cell.connect();
        }
    });
    add(std::move(top));

    // Where each serial controller is plugged in: the ports there are now,
    // by the stable name a reboot does not change.
    std::vector<JPSerialPorts::Port> ports;
    for (const JPDriverConfig& d : cell.config().drivers) {
        if (d.link["type"].str() != "serial") continue;
        if (ports.empty()) ports = JPSerialPorts::list();
        const std::string current = d.link["port"].str();
        std::vector<std::string> labels, paths;
        int selected = -1;
        for (const JPSerialPorts::Port& p : ports) {
            if (p.path == current || JPSerialPorts::stablePath(current) == p.path) selected = int(labels.size());
            labels.push_back(p.label);
            paths.push_back(p.path);
        }
        if (selected < 0) {   // configured, but not plugged in now: still shown, so it is not silently lost
            selected = int(labels.size());
            labels.push_back(current + " (not found)");
            paths.push_back(current);
        }
        auto portRow = row(graph);
        portRow->add(std::make_unique<JLabel>(graph, d.name + " port"));
        JComboBox* combo = portRow->add(std::make_unique<JComboBox>(graph, labels));
        combo->setHSizePolicy(JSizePolicyMode::Expanding, 1);
        combo->setCurrentIndex(selected);
        const std::string id = d.id;
        combo->onIndexChanged.connect([this, id, paths](int i) {
            if (i >= 0 && size_t(i) < paths.size() && onPortChosen) onPortChosen(id, paths[size_t(i)]);
        });
        add(std::move(portRow));
    }

    // Position.
    add(std::make_unique<JLabel>(graph, "Position"));
    auto dro = form(graph);
    const auto positions = cell.positions();
    for (const JPAxisConfig& a : cell.config().axes) {
        dro->add(std::make_unique<JLabel>(graph, a.name));
        const auto p = positions.find(a.id);
        m_axisValues[a.id] = dro->add(std::make_unique<JLabel>(graph, p == positions.end() ? "" : formatCoordinate(p->second)));
    }
    add(std::move(dro));

    // Actuators.
    if (!cell.config().actuators.empty()) {
        add(std::make_unique<JLabel>(graph, "Actuators"));
        auto acts = form(graph);
        for (const JPActuatorConfig& a : cell.config().actuators) {
            acts->add(std::make_unique<JLabel>(graph, a.name));
            auto controls = row(graph);
            const std::string id = a.id;
            if (a.canSwitch()) {
                controls->add(button(graph, "On"))->onClicked.connect([this, id] { m_cell.switchActuator(id, true); });
                controls->add(button(graph, "Off"))->onClicked.connect([this, id] { m_cell.switchActuator(id, false); });
            }
            if (a.canRead())
                controls->add(button(graph, "Read"))->onClicked.connect([this, id] { m_cell.readActuator(id); });
            m_actuatorValues[a.id] = controls->add(std::make_unique<JLabel>(graph, ""));
            acts->add(std::move(controls));
        }
        add(std::move(acts));
    }

    // Console.
    add(std::make_unique<JLabel>(graph, "Console"));
    m_console = add(std::make_unique<JListView>(graph));
    m_console->setVSizePolicy(JSizePolicyMode::Expanding, 1);
    auto input = row(graph);
    if (cell.config().drivers.size() > 1) {
        std::vector<std::string> names;
        for (const JPDriverConfig& d : cell.config().drivers) names.push_back(d.name);
        m_controller = input->add(std::make_unique<JComboBox>(graph, names));
    }
    m_input = input->add(std::make_unique<JLineEdit>(graph, "G-code to send"));
    m_input->setHSizePolicy(JSizePolicyMode::Expanding, 1);
    m_input->onReturnPressed.connect([this] { sendConsoleLine(); });
    input->add(std::make_unique<JButton>(graph, "Send"))->onClicked.connect([this] { sendConsoleLine(); });
    add(std::move(input));

    m_disconnects.push_back(cell.onConnection.connect([this](bool ok, std::string why) {
        onMain([this, ok, why] { showConnection(ok, why); });
    }));
    m_disconnects.push_back(cell.onPositions.connect([this](std::map<std::string, double> p) {
        onMain([this, p] { showPositions(p); });
    }));
    m_disconnects.push_back(cell.onActuator.connect([this](std::string id, bool ok, std::string v) {
        onMain([this, id, ok, v] { showActuator(id, ok, v); });
    }));
    m_disconnects.push_back(cell.onTraffic.connect([this](std::string name, bool sent, std::string line) {
        onMain([this, name, sent, line] { addConsoleLine(name + (sent ? " \xE2\x86\x92 " : " \xE2\x86\x90 ") + line); });
    }));
    m_disconnects.push_back(cell.onAlarm.connect([this](std::string what) {
        onMain([this, what] { addConsoleLine("\xE2\x9A\xA0 " + what); });
    }));
    showConnection(cell.isConnected(), std::string());
}

JPMachinePanel::~JPMachinePanel() {
    for (const auto& d : m_disconnects) d();
    *m_alive = false;
}

void JPMachinePanel::onMain(std::function<void()> fn) {
    std::weak_ptr<bool> alive = m_alive;
    JMainThreadDispatcher::instance().post([alive, fn = std::move(fn)] {
        if (const auto a = alive.lock(); a && *a) fn();
    });
}

void JPMachinePanel::showConnection(bool connected, const std::string& why) {
    std::string text = m_cell.config().name + (connected ? ": connected" : ": not connected");
    if (connected)
        for (const auto& [id, fw] : m_cell.firmware()) text += " \xC2\xB7 " + fw;
    if (!why.empty()) text += " (" + why + ")";
    m_status->setText(text);
    m_connect->setLabel(connected ? "Disconnect" : "Connect");
}

void JPMachinePanel::showPositions(const std::map<std::string, double>& positions) {
    for (const auto& [id, value] : positions)
        if (const auto it = m_axisValues.find(id); it != m_axisValues.end()) it->second->setText(formatCoordinate(value));
}

void JPMachinePanel::showActuator(const std::string& id, bool ok, const std::string& value) {
    if (const auto it = m_actuatorValues.find(id); it != m_actuatorValues.end())
        it->second->setText(ok ? value : "\xE2\x9A\xA0 " + value);
}

// Newest first: the line just sent or received is always the visible one.
// (JListView resets its scroll on setItems and has no call to scroll to a
// row, so oldest-first would leave the newest line out of sight.)
void JPMachinePanel::addConsoleLine(const std::string& line) {
    m_consoleLines.insert(m_consoleLines.begin(), line);
    if (m_consoleLines.size() > kConsoleLines) m_consoleLines.resize(kConsoleLines);
    m_console->setItems(m_consoleLines);
}

void JPMachinePanel::sendConsoleLine() {
    const std::string line = m_input->text();
    if (line.empty() || m_cell.config().drivers.empty()) return;
    const size_t which = m_controller ? size_t(std::max(0, m_controller->currentIndex())) : 0;
    m_cell.sendLine(m_cell.config().drivers[std::min(which, m_cell.config().drivers.size() - 1)].id, line);
    m_input->setText("");
}

} // inline namespace jf
