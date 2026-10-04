// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPIconButton.h"
#include "JPPartsTableModel.h"
#include "JPTable.h"

#include "model/JPConfiguration.h"

#include <j/core/JContainer.h>
#include <j/core/JLineEdit.h>
#include <j/core/JTabWidget.h>
#include <j/core/Splitter.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

inline namespace jf {

// The Parts tab, as OpenPnP's PartsPanel: a toolbar (New Part…, Delete
// Part, Pick Part, Copy Part to Clipboard, Create Part from Clipboard), a
// search box, the parts table, and under it the chosen part's tabs
// (Settings: its pick conditions).
class JPPartsPanel : public JContainer {
public:
    // `split`: the table's share of the height, as last left.
    JPPartsPanel(JSceneGraph& graph, JPConfiguration& config, double split);

    // A part was added, changed or deleted (to be saved, other views told).
    std::function<void()> onChanged;
    // Feed and pick the part from the first feeder that has it.
    std::function<void(const JPPart&)> onPickPart;
    // Opens a menu at window coordinates (a table cell's choices).
    std::function<void(JMenu*, float x, float y)> openMenu;

    // The parts changed elsewhere: shown again, the selection kept.
    void refresh();
    void selectPart(const JPPart* part);
    const JPPart* selectedPart() const;
    double split() const;

private:
    std::vector<JPPart*> selections() const;
    void updateWizards();
    void newPart();
    void deleteParts();
    void copyPart();
    void pastePart();
    void changed();

    JPConfiguration&                   m_config;
    JPPartsTableModel                  m_model;
    JPTable*                           m_table = nullptr;
    JLineEdit*                         m_search = nullptr;
    JSplitter*                         m_split = nullptr;
    std::unique_ptr<JContainer>        m_tablePane, m_tabsPane;
    JTabWidget*                        m_tabs = nullptr;
    std::unique_ptr<JContainer>        m_settings;   // the Settings tab's page
    JPIconButton*                      m_delete = nullptr;
    JPIconButton*                      m_pick = nullptr;
    JPIconButton*                      m_copy = nullptr;
    std::string                        m_shownPart;   // whose tabs are shown
    int                                m_lastTab = 0;
};

} // inline namespace jf
