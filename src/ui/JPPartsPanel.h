// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPIconButton.h"
#include "JPPartsTableModel.h"
#include "JPSetupForm.h"
#include "JPTable.h"

#include "model/JPConfiguration.h"
#include "setup/JPVisionForms.h"

#include <j/core/JContainer.h>
#include <j/core/JLineEdit.h>
#include <j/core/Splitter.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

inline namespace jf {

// The Parts tab, as OpenPnP's PartsPanel: a toolbar (New Part…, Delete
// Part, Pick Part, Copy Part to Clipboard, Create Part from Clipboard), a
// search box, the parts table, and under it the chosen part's tabs
// (Settings: its pick conditions; Bottom Vision Settings and Fiducial
// Vision Settings: those it uses, JPVisionForms, to specialize for it).
class JPPartsPanel : public JContainer {
public:
    // `split`: the table's share of the height, as last left.
    JPPartsPanel(JSceneGraph& graph, JPConfiguration& config, double split);

    // A part was added, changed or deleted (to be saved, other views told).
    std::function<void()> onChanged;
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
    void updateWizards();
    void newPart();
    void deleteParts();
    void copyPart();
    void pastePart();
    void changed();
    void act(const std::string& action);

    JPConfiguration&                   m_config;
    JPPartsTableModel                  m_model;
    JPTable*                           m_table = nullptr;
    JLineEdit*                         m_search = nullptr;
    JSplitter*                         m_split = nullptr;
    std::unique_ptr<JContainer>        m_tablePane, m_tabsPane;
    JPSetupForm*                       m_form = nullptr;   // the part's tabs
    JPIconButton*                      m_delete = nullptr;
    JPIconButton*                      m_pick = nullptr;
    JPIconButton*                      m_copy = nullptr;
    std::string                        m_shownPart;   // whose tabs are shown
};

} // inline namespace jf
