// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPIconButton.h"
#include "JPSetupForm.h"
#include "JPTable.h"
#include "JPVisionSettingsTableModel.h"

#include "model/JPConfiguration.h"
#include "setup/JPVisionForms.h"

#include <j/core/JComboBox.h>
#include <j/core/JContainer.h>
#include <j/core/Splitter.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

inline namespace jf {

// The Vision tab, as OpenPnP's VisionSettingsPanel: a toolbar (New
// Settings, Delete Settings, Copy Vision Settings to Clipboard, Create Vision
// Settings from Clipboard) and the Type shown (BottomVision or
// FiducialVision), the settings table (JPVisionSettingsTableModel) and the
// chosen one's page (JPVisionForms).
class JPVisionSettingsPanel : public JContainer {
public:
    JPVisionSettingsPanel(JSceneGraph& graph, JPConfiguration& config, double split);

    std::function<void()> onChanged;
    // The machine's default vision settings ids (bottom, fiducial), for Assigned To.
    std::function<std::pair<std::string, std::string>()> machineDefaults;
    // A setting's pipeline in the Pipeline Editor; a parameter's slider moved
    // (its effect to show). With the part or package the page is for.
    std::function<void(const std::string& settingsId, const JPVisionForms::Holder& holder)> editPipeline;
    std::function<void(const std::string& settingsId, const JPVisionForms::Holder& holder, const std::string& parameter)>
        previewParameter;
    // A parameter's slider moved: the setting to be saved (the pages not shown again).
    std::function<void()> onParameterChanged;
    // A test on the machine (Test Alignment, Detect Offsets, Test Fiducial
    // Locator), and what the tests work with (without it, they are not offered).
    std::function<void(const std::string& settingsId, const JPVisionForms::Holder& holder, const std::string& test)> visionTest;
    void setTests(JPVisionForms::Tests tests) { m_tests = std::move(tests); }

    void refresh();
    double split() const;
    // A button of a settings' page (prefixed "bottom:" or "fiducial:"), or a
    // parameter's slider moved, for the settings `settingsId`; shown here or on
    // Machine Setup's vision nodes (as the machine's default).
    void act(const std::string& settingsId, const std::string& action);

private:
    JPVisionForms::Tests m_tests;
    const JPVisionForms::Tests* tests() const { return m_tests.angle ? &m_tests : nullptr; }
    std::vector<JPVisionSettings*> selections() const;
    std::string usedIn(const JPVisionSettings& v) const;
    void showForm();
    void newSettings();
    void deleteSettings();
    void copySettings();
    void pasteSettings();
    void changed();

    JPConfiguration&            m_config;
    JPVisionSettingsTableModel  m_model;
    JPTable*                    m_table = nullptr;
    JComboBox*                  m_type = nullptr;
    JSplitter*                  m_split = nullptr;
    std::unique_ptr<JContainer> m_tablePane, m_formPane;
    JPSetupForm*                m_form = nullptr;
    std::string                 m_shown;
    JPIconButton*               m_delete = nullptr;
    JPIconButton*               m_copy = nullptr;
};

} // inline namespace jf
