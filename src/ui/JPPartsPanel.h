// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPIconButton.h"
#include "JPPartsTableModel.h"
#include "JPSetupForm.h"
#include "setup/JPFormBuilder.h"
#include "JPTable.h"

#include "model/JPConfiguration.h"
#include "setup/JPVisionForms.h"

#include <j/core/JComboBox.h>
#include <j/core/JContainer.h>
#include <j/core/JLineEdit.h>
#include <j/core/MenuSystem.h>
#include <j/core/Splitter.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

inline namespace jf {

// The Parts tab, as OpenPnP's PartsPanel: a toolbar (New Part…, Delete
// Part, Pick Part, Copy Part to Clipboard, Create Part from Clipboard), a
// search box, the parts table (the library's and each open board's,
// JPPartsTableModel), and under it the chosen part's tabs (Settings: its
// pick conditions; Bottom Vision Settings and Fiducial Vision Settings:
// those it uses, JPVisionForms, to specialize for it; the library's also
// Library and Stock). A board's copy of a library part, or one to be chosen,
// says what it is instead. Right-click: Add to Library (a board's own),
// Update from Library (a board's copy the library has changed since).
class JPPartsPanel : public JContainer {
public:
    // `split`: the table's share of the height, as last left.
    JPPartsPanel(JSceneGraph& graph, JPConfiguration& config, double split);

    // A library part was added, changed or deleted (to be saved, other views told).
    std::function<void()> onChanged;
    // An open board's own part changed (Status Own), or its copy taken again: the board to be saved.
    std::function<void(JPBoard&)> onBoardChanged;
    // Feed and pick the part from the first feeder that has it.
    std::function<void(const JPPart&)> onPickPart;
    // The machine's default vision settings ids (bottom, fiducial).
    std::function<std::pair<std::string, std::string>()> machineDefaults;
    // A vision setting's pipeline in the Pipeline Editor; a parameter's
    // slider moved (its effect to show). With the part or package the page is for.
    std::function<void(const std::string& settingsId, const JPVisionForms::Holder& holder)> editPipeline;
    std::function<void(const std::string& settingsId, const JPVisionForms::Holder& holder, const std::string& parameter)>
        previewParameter;
    // A parameter's slider moved: the setting to be saved (the pages not shown again).
    std::function<void()> onParameterChanged;
    // A test on the machine (Test Alignment, Detect Offsets, Test Fiducial
    // Locator), and what the tests work with (without it, they are not offered).
    std::function<void(const std::string& settingsId, const JPVisionForms::Holder& holder, const std::string& test)> visionTest;
    void setTests(JPVisionForms::Tests tests) { m_tests = std::move(tests); }
    // Opens the library's manufacturers' names (JPlacerManufacturersDialog).
    std::function<void()> openManufacturers;
    // Receive Stock… of a library part (JPlacerStockReceiveDialog), and a lot's Ledger… (JPlacerLotLedgerDialog);
    // `changed` when either kept something.
    std::function<void(const JPPart&, std::function<void()> changed)> openReceive;
    std::function<void(const std::string& lotUuid, const std::string& partId, std::function<void()> changed)> openLedger;
    // Opens a menu at window coordinates (a table cell's choices).
    std::function<void(JMenu*, float x, float y)> openMenu;

    // The parts changed elsewhere: shown again, the selection kept.
    void refresh();
    void selectPart(const JPPart* part);
    // One part chosen in the table (for the tables linked to it, View > Selections in Tables).
    std::function<void(const JPPart&)> onPartChosen;
    const JPPart* selectedPart() const;
    double split() const;

private:
    JPVisionForms::Tests m_tests;
    const JPVisionForms::Tests* tests() const { return m_tests.angle ? &m_tests : nullptr; }
    // One of the pipeline's buttons or sliders: done (true), else not one of them.
    bool pipelineAct(const std::string& settingsId, const JPVisionForms::Holder& holder, const std::string& what);
    std::vector<JPPart*> selections() const;
    // The pages for a part (none: no pages).
    JPSetupProperties::Form formFor(const JPCatalog::Part* entry);
    // The one row chosen, and the board it lives in (none: several, none, or the library's).
    const JPCatalog::Part* selectedEntry() const;
    JPBoard*               selectedBoard() const;
    void addToLibrary();
    void updateFromLibrary();
    // The part's Library page: value, datasheet, identifiers, what boards call it.
    void libraryPage(JPFormBuilder& add, const std::string& partId);
    // One of its buttons (add, delete): done (true), else not one of them.
    bool libraryAct(const std::string& action);
    // The Stock page: the part's lots (their names, where kept, notes changed here, kept at once), on hand,
    // attrition set and measured; Receive Stock… and each lot's Ledger….
    void stockPage(JPFormBuilder& add, const std::string& partId);
    bool stockAct(const std::string& action);
    // The form made again, after this click, where it was.
    void remakeLater();
    // The vision settings a part's tabs show (its own, its package's, the machine's), in a word: when it changes
    // (settings deleted, specialized, generalized) the tabs are made again.
    std::string visionShown(const JPPart* p) const;
    void updateWizards();
    void newPart();
    void deleteParts();
    void copyPart();
    void pastePart();
    // A change to be kept: the board's (`board`), else the library's.
    void changed(JPBoard* board = nullptr);
    void act(const std::string& action);

    std::shared_ptr<bool>              m_alive = std::make_shared<bool>(true);
    std::vector<JPStockLot>            m_lots;   // the Stock page's lots, as last read
    JPConfiguration&                   m_config;
    JPPartsTableModel                  m_model;
    JPTable*                           m_table = nullptr;
    JLineEdit*                         m_search = nullptr;
    JComboBox*                         m_show = nullptr;
    JSplitter*                         m_split = nullptr;
    std::unique_ptr<JContainer>        m_tablePane, m_tabsPane;
    JPSetupForm*                       m_form = nullptr;   // the part's tabs
    JPIconButton*                      m_delete = nullptr;
    JPIconButton*                      m_pick = nullptr;
    JPIconButton*                      m_copy = nullptr;
    std::unique_ptr<JMenu>             m_contextMenu;
    JMenuItem*                         m_addToLibrary = nullptr;
    JMenuItem*                         m_updateFromLibrary = nullptr;
    std::string                        m_shownPart;   // whose tabs are shown
    std::string                        m_shownVision; // the vision settings its tabs show (visionShown), to see them change
};

} // inline namespace jf
