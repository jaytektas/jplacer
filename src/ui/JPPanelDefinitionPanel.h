// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPGroupFrame.h"
#include "JPIconButton.h"
#include "JPLocationsTableModel.h"
#include "JPPlacementsTableModel.h"
#include "JPTable.h"

#include "model/JPConfiguration.h"
#include "model/JPJob.h"
#include "model/JPPanelLocation.h"

#include <j/core/MenuSystem.h>
#include <j/core/Splitter.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

inline namespace jf {

// The Panels tab's Panel Definition, as OpenPnP's PanelDefinitionPanel:
// the chosen panel's Children (Add Child… (a new or existing board or
// panel), Remove Child(ren), Create array of children…, the panel viewer;
// where each lies, its side, enabled and fiducial check; right-click:
// Replace child(ren)…, Set Side, Set Enabled, Set Check Fids) over its
// Alignment Fiducials/Placements (Add Fiducial…, Remove Fiducial(s)…, Use
// Children Fiducials…; right-click: Set Side, Set Enabled). Space turns
// the chosen child on or off.
class JPPanelDefinitionPanel : public JPGroupFrame {
public:
    JPPanelDefinitionPanel(JSceneGraph& graph, JPConfiguration& config, std::function<const JPJob*()> job);

    // The panel changed (or boards and panels were made for it).
    std::function<void()> onChanged;
    std::function<void(JMenu*, float x, float y)> openMenu;
    // The panel viewer, of the panel shown.
    std::function<void()> onViewPanel;
    // A board or panel chosen from those known, or a file of one;
    // `what`: "board" or "panel".
    std::function<void(const std::string& title, const std::string& what, std::function<void(std::string)> chosen)>
        chooseExisting;
    // The array generator, for the chosen child; `done` after OK.
    std::function<void(JPPanelLocation& panel, JPPlacementsHolderLocation& child, std::function<void()> done)> openArray;
    // The child fiducial selector; `chosen` has the unique ids picked.
    std::function<void(JPPanelLocation& panel, std::function<void(std::vector<std::string>)> chosen)> chooseChildFiducials;

    // The panel shown (a definition), or none.
    void setPanel(std::shared_ptr<JPPanel> panel);
    JPPanel* panel() const { return static_cast<JPPanel*>(m_root.holder.get()); }
    JPPanelLocation& root() { return m_root; }
    // The panel changed elsewhere: shown again.
    void refresh();

private:
    std::vector<JPPlacementsHolderLocation*> childSelections() const;
    std::vector<JPPlacement*> fiducialSelections() const;
    void updateActions();
    void buildMenus();
    void showAddMenu();
    void addBoard(const std::string& path, const char* errorTitle);
    void addPanel(const std::string& path, const char* errorTitle);
    void removeChildren();
    void replaceChildren();
    void addFiducial();
    void removeFiducials();
    void useChildFiducials();
    void changed();

    JPConfiguration&                  m_config;
    std::function<const JPJob*()>     m_job;
    JPPanelLocation                   m_root;
    JPLocationsTableModel             m_children;
    JPPlacementsTableModel            m_fiducials;
    JPTable*                          m_childTable = nullptr;
    JPTable*                          m_fiducialTable = nullptr;
    JSplitter*                        m_split = nullptr;
    std::unique_ptr<JContainer>       m_childrenPane, m_fiducialsPane;
    JPIconButton*                     m_add = nullptr;
    JPIconButton*                     m_remove = nullptr;
    JPIconButton*                     m_array = nullptr;
    JPIconButton*                     m_view = nullptr;
    JPIconButton*                     m_addFiducial = nullptr;
    JPIconButton*                     m_removeFiducial = nullptr;
    JPIconButton*                     m_useChildren = nullptr;
    std::unique_ptr<JMenu>            m_addMenu, m_childMenu, m_fiducialMenu;
    std::vector<std::unique_ptr<JMenu>> m_subMenus;
    JMenuItem*                        m_replace = nullptr;
};

} // inline namespace jf
