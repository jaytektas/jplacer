// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPPanelDefinitionPanel.h"
#include "JPPlacementsHoldersGroup.h"

#include "model/JPConfiguration.h"
#include "model/JPJob.h"

#include <j/core/JContainer.h>
#include <j/core/Splitter.h>

#include <functional>
#include <memory>

inline namespace jf {

// The Panels tab, as OpenPnP's PanelsPanel: the Panels group (Add Panel
// (Create New Panel…, Existing Panel), Remove Panel, Copy Panel…, Clean Up;
// the panels known) over the chosen panel's definition
// (JPPanelDefinitionPanel), a divider between to drag.
class JPPanelsPanel : public JContainer {
public:
    JPPanelsPanel(JSceneGraph& graph, JPConfiguration& config, std::function<const JPJob*()> job, double split);

    std::function<void()> onChanged;
    std::function<void(JMenu*, float x, float y)> openMenu;
    std::function<void(JPPlacementsHolder&, std::function<void()> then)> confirmSave;
    // The panel whose definition is shown changed (the viewer follows it).
    std::function<void(JPPanel*)> onPanelShown;

    JPPanelDefinitionPanel& definition() { return *m_definition; }
    void refresh();
    double split() const;

private:
    std::unique_ptr<JContainer> m_panelsPane, m_definitionPane;
    JPPlacementsHoldersGroup*   m_panels = nullptr;
    JPPanelDefinitionPanel*     m_definition = nullptr;
    JSplitter*                  m_split = nullptr;
    JPConfiguration&            m_config;
};

} // inline namespace jf
