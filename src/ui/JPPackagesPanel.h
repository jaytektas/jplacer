// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPFootprintTableModel.h"
#include "JPIconButton.h"
#include "JPNozzleTipsTableModel.h"
#include "JPPackagesTableModel.h"
#include "JPTable.h"

#include "model/JPConfiguration.h"
#include "setup/JPVisionForms.h"

#include <j/core/JContainer.h>
#include <j/core/JLineEdit.h>
#include <j/core/JTabWidget.h>
#include <j/core/Splitter.h>

#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

inline namespace jf {

// The Packages tab, as OpenPnP's PackagesPanel: a toolbar (New Package…,
// Delete Package, Copy Package to Clipboard, Create Package from
// Clipboard), a search box, the packages table, and under it the chosen
// package's tabs: Nozzle Tips, Settings (vacuum and blow off), Footprint
// (its settings, generators and pads) and Vision Compositing.
class JPPackagesPanel : public JContainer {
public:
    JPPackagesPanel(JSceneGraph& graph, JPConfiguration& config, double split);

    std::function<void()> onChanged;
    std::function<void(JMenu*, float x, float y)> openMenu;
    // The chosen package's footprint to draw over the cameras while the tab
    // shows (null: none), as OpenPnP's PackageVisionWizard draws it.
    std::function<void(const JPFootprint*)> onShowFootprint;
    // The machine's nozzle tips (id, name), for the Nozzle Tips tab.
    std::function<std::vector<std::pair<std::string, std::string>>()> nozzleTips;
    // The machine's default vision settings ids (bottom, fiducial).
    std::function<std::pair<std::string, std::string>()> machineDefaults;
    // A vision setting's pipeline in the Pipeline Editor; a parameter's
    // slider moved (its effect to show). With the part or package the page is for.
    std::function<void(const std::string& settingsId, const JPVisionForms::Holder& holder)> editPipeline;
    std::function<void(const std::string& settingsId, const JPVisionForms::Holder& holder, const std::string& parameter)>
        previewParameter;
    // A parameter's slider moved: the setting to be saved (the pages not shown again).
    std::function<void()> onParameterChanged;

    void refresh();
    void selectPackage(const JPPackage* package);
    const JPPackage* selectedPackage() const;
    double split() const;

private:
    // One of the pipeline's buttons or sliders: done (true), else not one of them.
    bool pipelineAct(const std::string& settingsId, const JPVisionForms::Holder& holder, const std::string& what);
    std::vector<JPPackage*> selections() const;
    void updateWizards(bool force = false);
    void newPackage();
    void deletePackages();
    void copyPackage();
    void pastePackage();
    void changed();
    void showFootprint();
    // The chosen package's tabs, made afresh.
    std::unique_ptr<JContainer> nozzleTipsTab(JPPackage& p);
    std::unique_ptr<JContainer> settingsTab(JPPackage& p);
    std::unique_ptr<JContainer> footprintTab(JPPackage& p);
    std::unique_ptr<JContainer> compositingTab(JPPackage& p);
    // Its Bottom or Fiducial Vision Settings (its own, else the machine's), to specialize for it.
    std::unique_ptr<JContainer> visionTab(JPPackage& p, JPVisionSettings::Kind kind);
    void visionAct(const std::string& action);
    void generatePads(JPFootprint::Generator type);

    JPConfiguration&                          m_config;
    JPPackagesTableModel                      m_model;
    JPFootprintTableModel                     m_padsModel;
    JPNozzleTipsTableModel                    m_tipsModel;
    JPTable*                                  m_table = nullptr;
    JPTable*                                  m_pads = nullptr;
    JLineEdit*                                m_search = nullptr;
    JSplitter*                                m_split = nullptr;
    std::unique_ptr<JContainer>               m_tablePane, m_tabsPane;
    JTabWidget*                               m_tabs = nullptr;
    std::vector<std::unique_ptr<JContainer>>  m_pages;
    JPIconButton*                             m_delete = nullptr;
    JPIconButton*                             m_copy = nullptr;
    JPIconButton*                             m_padDelete = nullptr;
    JPIconButton*                             m_padMark = nullptr;
    std::string                               m_shown;   // the package whose tabs are shown
    int                                       m_lastTab = 0;
};

} // inline namespace jf
