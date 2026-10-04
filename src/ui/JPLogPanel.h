// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPTable.h"
#include "JPTableModel.h"

#include "common/JPLogLevels.h"

#include <j/core/JCheckBox.h>
#include <j/core/JComboBox.h>
#include <j/core/JContainer.h>
#include <j/core/JLineEdit.h>

#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

inline namespace jf {

// The Log tab, as OpenPnP's LogPanel: Global Logging Settings (the Global
// Log Level, the log's own, also the Console's Log), then Filter Logging
// Panel (Search: text anywhere in an entry, without regard to case; Log
// Level: that level and above; System Output: what libraries and the
// framework log besides jplacer's own; Clear log, Copy to clipboard, Scroll
// down), over the log's entries as they come, each a line ("2026-10-05
// 12:00:00.123 job INFO: …") coloured by its level, the newest followed
// while the list is at its end. Ctrl+C copies the entries chosen.
class JPLogPanel : public JContainer {
public:
    explicit JPLogPanel(JSceneGraph& graph);
    ~JPLogPanel() override;

    // The log's levels changed (the Global Log Level): for the owner to keep.
    std::function<void(const JPLogLevels&)> onLogLevels;

private:
    struct Entry {
        std::string line, category;
        JLogLevel   level;
    };
    class Model;

    void take();
    void filter();
    std::string filteredText() const;

    std::vector<Entry>                m_entries;
    std::unique_ptr<Model>            m_model;
    JPTable*                          m_table = nullptr;
    JLineEdit*                        m_search = nullptr;
    JComboBox*                        m_globalLevel = nullptr;
    JComboBox*                        m_levelFilter = nullptr;
    JCheckBox*                        m_systemOut = nullptr;
    int                               m_listener = 0;
    struct Inbox {
        std::mutex         mutex;
        std::vector<Entry> entries;
    };
    std::shared_ptr<Inbox>            m_inbox = std::make_shared<Inbox>();
    std::shared_ptr<bool>             m_alive = std::make_shared<bool>(true);
};

} // inline namespace jf
