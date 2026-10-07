// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPConsolePanel.h"

#include "JPHistoryLineEdit.h"

#include "JPMenuButton.h"
#include "JPUiParts.h"

#include "common/JPlacerLog.h"

#include <j/core/JStyle.h>
#include <j/core/JTextHelper.h>
#include <j/core/Log.h>
#include <j/core/MainThreadDispatcher.h>

#include <algorithm>
#include <cctype>
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
        refilter();
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
    clear->onClicked.connect([this] { this->clear(); });
    add(std::move(shows));

    m_text = add(std::make_unique<JTextArea>(graph));
    m_text->setReadOnly(true);
    m_text->setMouseSelection(true);
    m_text->setVSizePolicy(JSizePolicyMode::Expanding, 1);
    m_text->setHSizePolicy(JSizePolicyMode::Expanding, 1);
    m_textMenu = std::make_unique<JMenu>("Console");
    m_textMenu->add(graph, "Copy")->onTriggered.connect([this] { m_text->copySelection(); });
    m_textMenu->add(graph, "Select All")->onTriggered.connect([this] { m_text->selectAll(); });
    m_textMenu->addSeparator(graph);
    m_textMenu->add(graph, "Clear")->onTriggered.connect([this] { this->clear(); });
    m_text->setContextMenu(m_textMenu.get());
    auto input = JPUiParts::row(graph);
    if (cell.config().drivers.size() > 1) {
        std::vector<std::string> names;
        for (const JPDriverConfig& d : cell.config().drivers) names.push_back(d.name);
        m_controller = input->add(std::make_unique<JComboBox>(graph, names));
    }
    m_input = input->add(std::make_unique<JPHistoryLineEdit>(graph, "G-code to send"));
    m_input->setHSizePolicy(JSizePolicyMode::Expanding, 1);
    m_input->onReturnPressed.connect([this] { send(); });
    input->add(JPUiParts::button(graph, "Send"))->onClicked.connect([this] { send(); });
    // OpenPnP's Force Upper Case: on, as most controllers want their commands.
    const std::string upper = "Force Upper Case";
    m_upperCase = input->add(std::make_unique<JCheckBox>(
        graph, upper, st.checkHeight + 2 * st.spacing + std::ceil(JTextHelper::measureWidth(upper))));
    m_upperCase->setChecked(true);
    add(std::move(input));

    m_watch.on(cell.onTraffic, [this](std::string name, bool sent, std::string line) {
        // Kept even while not shown: ticking G-code shows what passed meanwhile.
        addLines({ Line { true, JLogLevel::Info, "", name + (sent ? " \xE2\x86\x92 " : " \xE2\x86\x90 ") + line } });
    });
    // The log, from any thread: kept, then taken in on the main thread, each line as the log file has it
    // ("[INFO][machine.cell] ..."). Its traffic category is left out: the G-code box shows that.
    std::weak_ptr<bool> alive = m_alive;
    m_listener = JLog::instance().addListener([this, alive, inbox = m_inbox](JLogLevel level, const std::string& cat,
                                                                              const std::string& msg) {
        if (cat == JPlacerLog::kTraffic) return;
        bool first;
        {
            std::lock_guard lk(inbox->mutex);
            first = inbox->lines.empty();
            inbox->lines.push_back(Line { false, level, cat, std::string("[") + jLogLevelName(level) + "][" + cat + "] " + msg });
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
    std::vector<Line> lines;
    {
        std::lock_guard lk(m_inbox->mutex);
        lines.swap(m_inbox->lines);
    }
    addLines(std::move(lines));
}

void JPConsolePanel::levelsChanged() {
    m_levels = JPLogLevels::current();
    if (onLogLevels) onLogLevels(m_levels);
    refilter();
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
// the end, and stays on the same lines while the reader has scrolled back
// (the oldest going as the history fills); a selection stays on its text.
void JPConsolePanel::addLines(std::vector<Line> lines) {
    if (lines.empty()) return;
    // One line to a row, a newline between them (none after the last: no empty row at the end). A shown
    // line's size counts the newline after it, there once the next comes.
    std::string more;
    bool any = !m_text->text().empty();
    for (Line& l : lines) {
        const bool shown = shows(l);
        if (shown) {
            if (any) more += "\n";
            more += l.text;
            any = true;
        }
        m_shownSizes.push_back(shown ? l.text.size() + 1 : 0);
        m_lines.push_back(std::move(l));
    }
    m_text->appendText(more);
    size_t gone = 0;
    while (m_lines.size() > kLines) {
        gone += m_shownSizes.front();
        m_shownSizes.pop_front();
        m_lines.pop_front();
    }
    m_text->dropFront(std::min(gone, m_text->text().size()));
}

bool JPConsolePanel::shows(const Line& line) const {
    if (line.traffic) return m_showTraffic;
    const auto own = m_levels.own.find(line.category);
    return line.level >= (own == m_levels.own.end() ? m_levels.global : own->second);
}

void JPConsolePanel::refilter() {
    std::string text;
    for (size_t i = 0; i < m_lines.size(); ++i) {
        const bool shown = shows(m_lines[i]);
        if (shown) {
            if (!text.empty()) text += "\n";
            text += m_lines[i].text;
        }
        m_shownSizes[i] = shown ? m_lines[i].text.size() + 1 : 0;
    }
    m_text->setText(text);
    m_text->scrollToEnd();
}

void JPConsolePanel::clear() {
    m_lines.clear();
    m_shownSizes.clear();
    m_text->setText("");
}

void JPConsolePanel::send() {
    std::string line = m_input->text();
    if (line.empty() || m_cell.config().drivers.empty()) return;
    m_input->remember(line);
    if (m_upperCase->isChecked())
        std::transform(line.begin(), line.end(), line.begin(), [](unsigned char ch) { return char(std::toupper(ch)); });
    JLOGC(JPlacerLog::kUi, JLogLevel::Info) << "console: " << line;
    const size_t which = m_controller ? size_t(std::max(0, m_controller->currentIndex())) : 0;
    m_cell.sendLine(m_cell.config().drivers[std::min(which, m_cell.config().drivers.size() - 1)].id, line);
    m_input->setText("");
}

} // inline namespace jf
