// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPConsolePanel.h"

#include "JPMenuButton.h"
#include "JPUiParts.h"

#include "common/JPlacerLog.h"

#include <j/core/JStyle.h>
#include <j/core/JTextHelper.h>
#include <j/core/Log.h>
#include <j/core/MainThreadDispatcher.h>

#include <algorithm>
#include <cmath>

inline namespace jf {

namespace {

// How much history is kept: enough to scroll back through a homing or a
// settings dump, bounded so a long session does not grow without end.
constexpr size_t kLines = 1000;

// The Log box's words for the levels (JPLogLevels::choices, in order).
std::string shown(JLogLevel l) {
    switch (l) {
        case JLogLevel::Off:   return "Off";
        case JLogLevel::Error: return "Errors";
        case JLogLevel::Warn:  return "Warnings";
        case JLogLevel::Info:  return "Info";
        case JLogLevel::Debug: return "Debug";
        case JLogLevel::Trace: return "Trace";
    }
    return "Info";
}

} // namespace

JPConsolePanel::JPConsolePanel(JSceneGraph& graph, JPCell& cell, bool showTraffic)
    : JContainer(graph), m_cell(cell), m_showTraffic(showTraffic) {
    JPUiParts::asPanel(*this);

    // What is shown.
    auto shows = JPUiParts::row(graph);
    // As wide as its box and its words.
    const JStyle& st = JStyle::current();
    m_traffic = shows->add(std::make_unique<JCheckBox>(
        graph, "G-code", st.checkHeight + 2 * st.spacing + std::ceil(JTextHelper::measureWidth("G-code"))));
    m_traffic->setChecked(showTraffic);
    m_traffic->onStateChanged.connect([this](bool on) {
        m_showTraffic = on;
        if (onShowTraffic) onShowTraffic(on);
    });
    JLabel* log = shows->add(std::make_unique<JLabel>(graph, "Log", std::ceil(JTextHelper::measureWidth("Log")) + st.spacing));
    log->setHSizePolicy(JSizePolicyMode::Fixed);
    std::vector<std::string> levels;
    for (JLogLevel l : JPLogLevels::choices()) levels.push_back(shown(l));
    m_level = shows->add(std::make_unique<JComboBox>(graph, levels));
    const JLogLevel now = JLog::instance().globalLevel();
    for (size_t i = 0; i < JPLogLevels::choices().size(); ++i)
        if (JPLogLevels::choices()[i] == now) m_level->setCurrentIndex(int(i));
    m_level->onIndexChanged.connect([this](int i) {
        if (i < 0 || i >= int(JPLogLevels::choices().size())) return;
        JPLogLevels l = JPLogLevels::current();
        l.global = JPLogLevels::choices()[size_t(i)];
        l.apply();
        levelsChanged();
    });
    m_categories = shows->add(std::make_unique<JPMenuButton>(graph, "Categories"));
    m_categories->onClicked.connect([this] { showCategories(); });
    auto clear = shows->add(JPUiParts::button(graph, "Clear"));
    clear->onClicked.connect([this] {
        m_lines.clear();
        m_list->setItems(m_lines);
    });
    add(std::move(shows));

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
        if (m_showTraffic) addLine(name + (sent ? " \xE2\x86\x92 " : " \xE2\x86\x90 ") + line);
    });
    // The log, from any thread: kept, then taken in on the main thread. Its
    // traffic category is left out: the G-code box shows that.
    std::weak_ptr<bool> alive = m_alive;
    m_listener = JLog::instance().addListener([this, alive, inbox = m_inbox](JLogLevel level, const std::string& cat,
                                                                              const std::string& msg) {
        if (cat == JPlacerLog::kTraffic) return;
        const std::string tag = level >= JLogLevel::Warn ? "\xE2\x9A\xA0 " : level == JLogLevel::Info ? "" : JPLogLevels::name(level) + " ";
        bool first;
        {
            std::lock_guard lk(inbox->mutex);
            first = inbox->lines.empty();
            inbox->lines.push_back(tag + cat + ": " + msg);
        }
        if (first)
            JMainThreadDispatcher::instance().post([this, alive] {
                if (const auto b = alive.lock(); b && *b) takeLogLines();
            });
    });
}

JPConsolePanel::~JPConsolePanel() {
    *m_alive = false;
    JLog::instance().removeListener(m_listener);
}

void JPConsolePanel::takeLogLines() {
    std::vector<std::string> lines;
    {
        std::lock_guard lk(m_inbox->mutex);
        lines.swap(m_inbox->lines);
    }
    for (const std::string& l : lines) addLine(l);
}

void JPConsolePanel::levelsChanged() {
    if (onLogLevels) onLogLevels(JPLogLevels::current());
}

void JPConsolePanel::showCategories() {
    // Made each time it opens: the categories used so far, each with its level.
    JSceneGraph& g = m_graph;
    m_menu = std::make_unique<JMenu>("Categories");
    m_submenus.clear();
    const JPLogLevels now = JPLogLevels::current();
    JMenuItem* reset = m_menu->add(g, "All as Log");
    reset->setEnabled(!now.own.empty());
    reset->onTriggered.connect([this] {
        JPLogLevels l = JPLogLevels::current();
        l.own.clear();
        l.apply();
        levelsChanged();
    });
    m_menu->addSeparator(g);
    for (const std::string& c : JPLogLevels::categories()) {
        const auto own = now.own.find(c);
        auto sub = std::make_unique<JMenu>(c);
        JMenuItem* asLog = sub->add(g, "As Log (" + shown(now.global) + ")");
        asLog->setCheckable(true);
        asLog->setChecked(own == now.own.end());
        asLog->onTriggered.connect([this, c] {
            JPLogLevels l = JPLogLevels::current();
            l.own.erase(c);
            l.apply();
            levelsChanged();
        });
        for (JLogLevel level : JPLogLevels::choices()) {
            JMenuItem* item = sub->add(g, shown(level));
            item->setCheckable(true);
            item->setChecked(own != now.own.end() && own->second == level);
            item->onTriggered.connect([this, c, level] {
                JPLogLevels l = JPLogLevels::current();
                l.own[c] = level;
                l.apply();
                levelsChanged();
            });
        }
        m_menu->add(g, c + (own == now.own.end() ? std::string() : "  (" + shown(own->second) + ")"), {}, sub.get());
        m_submenus.push_back(std::move(sub));
    }
    const JRect b = m_categories->bounds();
    if (openMenu) openMenu(m_menu.get(), b.x, b.y + b.height);
}

// Newest last, as a terminal: the view follows each new line while it is at
// the end, and stays where it is while the reader has scrolled back (the
// oldest lines going as the history fills, the view kept on the same ones).
void JPConsolePanel::addLine(const std::string& line) {
    const bool following = m_list->isAtEnd();
    const float at = m_list->scrollY();
    m_lines.push_back(line);
    size_t dropped = 0;
    if (m_lines.size() > kLines) {
        dropped = m_lines.size() - kLines;
        m_lines.erase(m_lines.begin(), m_lines.begin() + long(dropped));
    }
    m_list->setItems(m_lines);
    if (following) m_list->scrollToEnd();
    else m_list->setScrollY(at - float(dropped) * m_list->rowHeight());
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
