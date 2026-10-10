// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPFootprintTableModel.h"
#include "JPCompositingPreview.h"
#include "JPIconButton.h"
#include "JPNozzleTipsTableModel.h"
#include "JPPackagesTableModel.h"
#include "JPTable.h"

#include "model/JPConfiguration.h"
#include "setup/JPVisionForms.h"

#include <j/core/JComboBox.h>
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
// Clipboard), a search box, the packages table (the library's and each open
// board's, JPPackagesTableModel), and under it the chosen package's tabs:
// Nozzle Tips, Settings (vacuum and blow off), Footprint (its settings,
// generators and pads), the library's Footprints (a library package's only)
// and Vision Compositing. A board's copy of a library package says what it is
// instead; a board's own, edited, is given to every part of the board that
// shares it.
class JPPackagesPanel : public JContainer {
public:
    JPPackagesPanel(JSceneGraph& graph, JPConfiguration& config, double split);

    // A library package changed (to be saved, other views told).
    std::function<void()> onChanged;
    // An open board's own package changed: the board to be saved.
    std::function<void(JPBoard&)> onBoardChanged;
    std::function<void(JMenu*, float x, float y)> openMenu;
    // The chosen package's footprint to draw over the cameras (null: none
    // chosen), as OpenPnP's PackageVisionWizard draws it: told when another
    // is chosen (here, or by linked tables) or it is edited, not when the
    // tab is left.
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
    // A test on the machine (Test Alignment, Detect Offsets, Test Fiducial
    // Locator), and what the tests work with (without it, they are not offered).
    std::function<void(const std::string& settingsId, const JPVisionForms::Holder& holder, const std::string& test)> visionTest;
    // OpenPnP's Vision Compositing preview: the composite worked out for a
    // package with the machine's camera looking up and the first nozzle tip
    // that can take the package; false with why when there is none.
    struct CompositePreview {
        std::shared_ptr<const JPVisionComposite> composite;
        JPFootprint                              footprintMm;
        double cameraWidthMm = 0, cameraHeightMm = 0, roamingRadiusMm = 0;
    };
    std::function<bool(const JPPackage&, CompositePreview&, std::string& why)> computeComposite;
    void setTests(JPVisionForms::Tests tests) { m_tests = std::move(tests); }

    void refresh();
    void selectPackage(const JPPackage* package);
    const JPPackage* selectedPackage() const;
    double split() const;

private:
    JPVisionForms::Tests m_tests;
    // The shown Vision Compositing tab computed again (its settings or the footprint changed); none when not shown.
    std::function<void()> m_computeComposite;
    const JPVisionForms::Tests* tests() const { return m_tests.angle ? &m_tests : nullptr; }
    // One of the pipeline's buttons or sliders: done (true), else not one of them.
    bool pipelineAct(const std::string& settingsId, const JPVisionForms::Holder& holder, const std::string& what);
    std::vector<JPPackage*> selections() const;
    void updateWizards(bool force = false);
    void newPackage();
    void deletePackages();
    void copyPackage();
    void pastePackage();
    // A change to be kept: the chosen package's board (or `board`), else the library's.
    void changed();
    void changed(JPBoard* board);
    const JPCatalog::Package* selectedEntry() const;
    // A board's copy of the library's: what it is.
    std::unique_ptr<JContainer> copyTab(const JPCatalog::Package& entry);
    // onShowFootprint told of the chosen package's when another is chosen (`again`: anyway, it changed).
    void showFootprint(bool again = false);
    // The chosen package's tabs, made afresh.
    std::unique_ptr<JContainer> nozzleTipsTab(JPPackage& p);
    std::unique_ptr<JContainer> settingsTab(JPPackage& p);
    std::unique_ptr<JContainer> footprintTab(JPPackage& p);
    // The package's footprints in the library (land patterns, JPLibraryFootprint): each its name, the names
    // CAD files give it, its zero rotation, its pads; made from the package's own, from a KiCad file.
    std::unique_ptr<JContainer> footprintsTab(JPPackage& p);
    // The pages made again after the click that changed what they show.
    void remakeLater();
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
    JComboBox*                                m_show = nullptr;
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
    std::string                               m_footprintShown;   // the package onShowFootprint was last told of
    int                                       m_lastTab = 0;
    std::shared_ptr<bool>                     m_alive = std::make_shared<bool>(true);
};

} // inline namespace jf
