// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPLogPanel.h"

#include "common/JPLogLine.h"

#include "JPGroupFrame.h"
#include "JPIconButton.h"
#include "JPUiParts.h"

#include "common/JPlacerLog.h"

#include <j/core/JLabel.h>
#include <j/core/JStyle.h>
#include <j/core/JTextHelper.h>
#include <j/core/Log.h>
#include <j/core/MainThreadDispatcher.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <ctime>

inline namespace jf {

namespace {

// OpenPnP's search box is twenty characters wide.
constexpr int kSearchColumns = 20;

std::string folded(std::string s) {
    for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

const std::vector<JLogLevel>& allLevels() {
    static const std::vector<JLogLevel> levels { JLogLevel::Trace, JLogLevel::Debug, JLogLevel::Info, JLogLevel::Warn,
                                                 JLogLevel::Error, JLogLevel::Off };
    return levels;
}

} // namespace

// The entries shown: one column, without headings, each coloured by its level.
class JPLogPanel::Model : public JPTableModel {
public:
    std::vector<const Entry*> shown;
    int    columnCount() const override { return 1; }
    Column column(int) const override { return { "Log", "", Kind::Text }; }
    int    rowCount() const override { return int(shown.size()); }
    std::string text(int row, int) const override { return shown[size_t(row)]->line; }
    std::string rowKey(int row) const override { return std::to_string(reinterpret_cast<uintptr_t>(shown[size_t(row)])); }
    // OpenPnP's colours by level, as the theme has them, readable on its background (OpenPnP's own are for a
    // white one: its blue Info and its errors' pale band cannot be read on a dark theme): Info and Debug in
    // the text's own colour, Trace dimmed, warnings and errors in the theme's warning and danger colours.
    const uint8_t* cellInk(int row, int) const override {
        switch (shown[size_t(row)]->level) {
            case JLogLevel::Trace: return Colors::TextSecondary;
            case JLogLevel::Warn:  return Colors::Warning;
            case JLogLevel::Error: return Colors::Danger;
            default:               return nullptr;
        }
    }
};

JPLogPanel::JPLogPanel(JSceneGraph& graph) : JContainer(graph), m_model(std::make_unique<Model>()) {
    JPUiParts::asPanel(*this);
    const JStyle& st = JStyle::current();
    std::vector<std::string> levels;
    for (JLogLevel l : allLevels()) levels.push_back(JPLogLine::levelName(l));
    auto label = [&](JContainer& row, const char* text) {
        JLabel* l = row.add(std::make_unique<JLabel>(graph, text));
        l->setFixedSize(JTextHelper::measureWidth(text) + st.spacing, st.controlHeight);
    };
    auto framed = [&](const char* title, std::unique_ptr<JContainer> row) {
        auto frame = std::make_unique<JPGroupFrame>(graph, title);
        row->setFixedSize(0.f, st.controlHeight);
        row->setHSizePolicy(JSizePolicyMode::Expanding, 1);
        frame->add(std::move(row));
        frame->setVSizePolicy(JSizePolicyMode::Fixed);
        frame->setSize(0.f, st.controlHeight + JPGroupFrame::extraHeight());
        add(std::move(frame));
    };

    // Global Logging Settings: the log's own level.
    auto global = JPUiParts::row(graph);
    label(*global, "Global Log Level:");
    m_globalLevel = global->add(std::make_unique<JComboBox>(graph, levels));
    const JLogLevel now = JLog::instance().globalLevel();
    for (size_t i = 0; i < allLevels().size(); ++i)
        if (allLevels()[i] == now) m_globalLevel->setCurrentIndex(int(i));
    m_globalLevel->onIndexChanged.connect([this](int i) {
        if (i < 0 || i >= int(allLevels().size())) return;
        JPLogLevels l = JPLogLevels::current();
        l.global = allLevels()[size_t(i)];
        l.apply();
        if (onLogLevels) onLogLevels(JPLogLevels::current());
    });
    framed("Global Logging Settings", std::move(global));

    // Filter Logging Panel.
    auto filters = JPUiParts::row(graph);
    label(*filters, "Search");
    m_search = filters->add(std::make_unique<JLineEdit>(graph, ""));
    m_search->setFixedSize(JTextHelper::measureWidth("M") * kSearchColumns, st.controlHeight);
    m_search->onTextChanged.connect([this](const std::string&) { filter(); });
    label(*filters, "Log Level:");
    m_levelFilter = filters->add(std::make_unique<JComboBox>(graph, levels));
    m_levelFilter->setCurrentIndex(0);   // TRACE: everything
    m_levelFilter->onIndexChanged.connect([this](int) { filter(); });
    m_systemOut = filters->add(std::make_unique<JCheckBox>(graph, "System Output"));
    m_systemOut->setChecked(true);
    m_systemOut->onStateChanged.connect([this](bool) { filter(); });
    filters->add(std::make_unique<JContainer>(graph, 0.f, 0.f))->setHSizePolicy(JSizePolicyMode::Expanding, 1);
    JPIconButton* clear = filters->add(std::make_unique<JPIconButton>(graph, "Clear", "general-remove", "Clear log"));
    clear->onClicked.connect([this] {
        m_entries.clear();
        filter();
    });
    JPIconButton* copy = filters->add(std::make_unique<JPIconButton>(graph, "Copy", "copy", "Copy to clipboard"));
    copy->onClicked.connect([this] { JWidget::clipboardSet(filteredText()); });
    JPIconButton* down = filters->add(std::make_unique<JPIconButton>(graph, "Scroll Down", "scroll-down", "Scroll down"));
    down->onClicked.connect([this] { m_table->scrollToEnd(); });
    framed("Filter Logging Panel", std::move(filters));

    m_table = add(std::make_unique<JPTable>(graph));
    m_table->setHeaderShown(false);
    m_table->setModel(m_model.get());
    m_table->setVSizePolicy(JSizePolicyMode::Expanding, 1);

    // The log, from any thread: kept, then taken in on the main thread.
    std::weak_ptr<bool> alive = m_alive;
    m_listener = JLog::instance().addListener([this, alive, inbox = m_inbox](JLogLevel level, const std::string& cat,
                                                                              const std::string& msg) {
        bool first;
        {
            std::lock_guard lk(inbox->mutex);
            first = inbox->entries.empty();
            inbox->entries.push_back({ JPLogLine::text(level, cat, msg), cat, level });
        }
        if (first)
            JMainThreadDispatcher::instance().post([this, alive] {
                if (const auto a = alive.lock(); a && *a) take();
            });
    });
}

JPLogPanel::~JPLogPanel() {
    *m_alive = false;
    JLog::instance().removeListener(m_listener);
}

void JPLogPanel::take() {
    std::vector<Entry> got;
    {
        std::lock_guard lk(m_inbox->mutex);
        got.swap(m_inbox->entries);
    }
    if (got.empty()) return;
    m_entries.insert(m_entries.end(), std::make_move_iterator(got.begin()), std::make_move_iterator(got.end()));
    filter();
}

void JPLogPanel::filter() {
    // Followed while at its end.
    const bool following = m_table->atEnd();
    const std::string search = folded(m_search->text());
    const int least = std::max(0, m_levelFilter->currentIndex());
    const bool system = m_systemOut->isChecked();
    const std::vector<std::string>& own = JPlacerLog::all();
    m_model->shown.clear();
    for (const Entry& e : m_entries) {
        if (int(e.level) < int(allLevels()[size_t(least)])) continue;
        if (!system && std::find(own.begin(), own.end(), e.category) == own.end()) continue;
        if (!search.empty() && folded(e.line).find(search) == std::string::npos) continue;
        m_model->shown.push_back(&e);
    }
    m_table->refresh();
    if (following) m_table->scrollToEnd();
}

std::string JPLogPanel::filteredText() const {
    std::string out;
    for (const Entry* e : m_model->shown) out += e->line + "\n";
    return out;
}

} // inline namespace jf
