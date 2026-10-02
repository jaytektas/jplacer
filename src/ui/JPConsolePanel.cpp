// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPConsolePanel.h"

#include "JPUiParts.h"

#include "common/JPlacerLog.h"

#include <j/core/Log.h>

#include <algorithm>

inline namespace jf {

namespace {

// How much history is kept: enough to scroll back through a homing or a
// settings dump, bounded so a long session does not grow without end.
constexpr size_t kLines = 1000;

} // namespace

JPConsolePanel::JPConsolePanel(JSceneGraph& graph, JPCell& cell)
    : JContainer(graph), m_cell(cell) {
    JPUiParts::asPanel(*this);
    m_list = add(std::make_unique<JListView>(graph));
    m_list->setVSizePolicy(JSizePolicyMode::Expanding, 1);
    auto input = JPUiParts::row(graph);
    if (cell.config().drivers.size() > 1) {
        std::vector<std::string> names;
        for (const JPDriverConfig& d : cell.config().drivers) names.push_back(d.name);
        m_controller = input->add(std::make_unique<JComboBox>(graph, names));
    }
    m_input = input->add(std::make_unique<JLineEdit>(graph, "G-code to send"));
    m_input->setHSizePolicy(JSizePolicyMode::Expanding, 1);
    m_input->onReturnPressed.connect([this] { send(); });
    input->add(JPUiParts::button(graph, "Send"))->onClicked.connect([this] { send(); });
    add(std::move(input));

    m_watch.on(cell.onTraffic, [this](std::string name, bool sent, std::string line) {
        addLine(name + (sent ? " \xE2\x86\x92 " : " \xE2\x86\x90 ") + line);
    });
    m_watch.on(cell.onAlarm, [this](std::string what) { addLine("\xE2\x9A\xA0 " + what); });
}

// Newest first: the line just sent or received is always the visible one.
// (JListView resets its scroll on setItems and has no call to scroll to a
// row, so oldest-first would leave the newest line out of sight.)
void JPConsolePanel::addLine(const std::string& line) {
    m_lines.insert(m_lines.begin(), line);
    if (m_lines.size() > kLines) m_lines.resize(kLines);
    m_list->setItems(m_lines);
}

void JPConsolePanel::send() {
    const std::string line = m_input->text();
    if (line.empty() || m_cell.config().drivers.empty()) return;
    JLOGC(JPlacerLog::kUi, JLogLevel::Info) << "console: " << line;
    const size_t which = m_controller ? size_t(std::max(0, m_controller->currentIndex())) : 0;
    m_cell.sendLine(m_cell.config().drivers[std::min(which, m_cell.config().drivers.size() - 1)].id, line);
    m_input->setText("");
}

} // inline namespace jf
