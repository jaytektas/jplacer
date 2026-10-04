// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPIconButton.h"
#include "JPSetupForm.h"
#include "JPTable.h"
#include "JPVisionSettingsTableModel.h"

#include "model/JPConfiguration.h"

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

    void refresh();
    double split() const;

private:
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
