// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPFeedersTableModel.h"
#include "JPIconButton.h"
#include "JPSetupForm.h"
#include "JPTable.h"

#include "model/JPConfiguration.h"

#include <j/core/JContainer.h>
#include <j/core/JLineEdit.h>
#include <j/core/MenuSystem.h>
#include <j/core/Splitter.h>

#include <array>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

inline namespace jf {

// The Feeders tab, as OpenPnP's FeedersPanel: a toolbar (New Feeder…,
// Delete Feeder…, Pick, Feed, Move Camera and Move Tool to the pick
// location), a search box, the feeders table (JPFeedersTableModel) with its
// right-click Set Enabled and Set Feed option, and under it the chosen
// feeder's setup (JPFeederForms), its location buttons taking the camera or
// the chosen nozzle there or taking where it is.
class JPFeedersPanel : public JContainer {
public:
    using Tool  = JPSetupForm::Tool;
    using Where = std::array<std::optional<double>, 4>;

    JPFeedersPanel(JSceneGraph& graph, JPConfiguration& config, double split);

    // A feeder added, changed or deleted (to be saved, other views told).
    std::function<void()> onChanged;
    std::function<void(JMenu*, float x, float y)> openMenu;
    // OpenPnP's ClassSelectionDialog: one of `classes` (their simple names shown) chosen, or none.
    std::function<void(const std::string& title, const std::string& description, const std::vector<std::string>& classes,
                       std::function<void(std::string)> chosen)>
        chooseClass;
    // The machine: where the camera or chosen nozzle is, taking it somewhere
    // at safe Z, and the chosen nozzle's pick at a place.
    std::function<Where(Tool)> whereIs;
    std::function<void(Tool, const Where&)> moveTo;
    std::function<void(const JPLocation&)> pickAt;
    // Whether the job uses a part (an enabled placement on an enabled board).
    std::function<bool(const std::string& partId)> partUsed;

    // The feeders changed elsewhere (imported, a job's part): shown again.
    void refresh();
    // OpenPnP's showFeederForPart: the search cleared and the part's feeder
    // chosen (an enabled one first), else a new feeder made for it.
    void showFeederForPart(const std::string& partId);
    double split() const;
    // A feeder chosen (and shown), by id.
    void selectFeeder(const std::string& id);
    // OpenPnP's pickFeeder: a feed, then the chosen nozzle's pick at its pick location.
    void pickFrom(JPFeeder& f);

private:
    std::vector<JPFeeder*> selections() const;
    JPFeeder* selection() const;
    void selectionChanged();
    void showForm();
    void buildMenu();
    void newFeeder(const std::string& partId);
    void deleteFeeders();
    // OpenPnP's feedFeeder: the feed, false (and why shown) when it fails.
    bool feed(JPFeeder& f);
    void pick();
    void moveToPick(Tool tool);
    void capture(const JPSetupProperties::Row& row, Tool tool);
    void goTo(const JPSetupProperties::Row& row, Tool tool);
    void changed();

    JPConfiguration&                    m_config;
    JPFeedersTableModel                 m_model;
    JPTable*                            m_table = nullptr;
    JLineEdit*                          m_search = nullptr;
    JSplitter*                          m_split = nullptr;
    std::unique_ptr<JContainer>         m_tablePane, m_formPane;
    JPSetupForm*                        m_form = nullptr;
    std::string                         m_shown;   // the feeder whose setup is shown
    JPIconButton*                       m_delete = nullptr;
    JPIconButton*                       m_pick = nullptr;
    JPIconButton*                       m_feed = nullptr;
    JPIconButton*                       m_moveCamera = nullptr;
    JPIconButton*                       m_moveTool = nullptr;
    std::unique_ptr<JMenu>              m_menu;
    std::vector<std::unique_ptr<JMenu>> m_subMenus;
    JMenuItem*                          m_setEnabled = nullptr;
    JMenuItem*                          m_setFeedOptions = nullptr;
};

} // inline namespace jf
