// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPPanelsPanel.h"

inline namespace jf {

JPPanelsPanel::JPPanelsPanel(JSceneGraph& graph, JPConfiguration& config, std::function<const JPJob*()> job,
                             double split)
    : JContainer(graph, 0.f, 0.f), m_config(config) {
    setDirection(JFlexDirection::Column)->setAlignItems(JAlignItems::Stretch);

    m_panelsPane = std::make_unique<JContainer>(graph, 0.f, 0.f);
    m_panelsPane->setDirection(JFlexDirection::Column)->setAlignItems(JAlignItems::Stretch);
    m_panels = m_panelsPane->add(std::make_unique<JPPlacementsHoldersGroup>(graph, config, JPPlacementsHolder::Kind::Panel, job));
    m_panels->openMenu = [this](JMenu* m, float x, float y) { if (openMenu) openMenu(m, x, y); };
    m_panels->confirmSave = [this](JPPlacementsHolder& h, std::function<void()> then) {
        if (confirmSave) confirmSave(h, std::move(then));
        else then();
    };
    m_panels->onChanged = [this] {
        m_definition->refresh();
        if (onChanged) onChanged();
    };
    m_panels->onShown = [this](JPPlacementsHolder* h) {
        std::shared_ptr<JPPanel> shown;
        for (const auto& p : m_config.panels())
            if (p.get() == h) shown = p;
        m_definition->setPanel(shown);
        if (onPanelShown) onPanelShown(shown.get());
    };

    m_definitionPane = std::make_unique<JContainer>(graph, 0.f, 0.f);
    m_definitionPane->setDirection(JFlexDirection::Column)->setAlignItems(JAlignItems::Stretch);
    m_definition = m_definitionPane->add(std::make_unique<JPPanelDefinitionPanel>(graph, config, job));
    m_definition->openMenu = [this](JMenu* m, float x, float y) { if (openMenu) openMenu(m, x, y); };
    m_definition->onChanged = [this] {
        m_panels->refresh();
        if (onChanged) onChanged();
    };

    m_split = add(std::make_unique<JSplitter>(graph, JSplitter::JOrientation::Vertical, 0.f, 0.f));
    m_split->setHostsPanes(true);
    m_split->setVSizePolicy(JSizePolicyMode::Expanding, 1);
    m_split->addPane(m_panelsPane.get(), float(split));
    m_split->addPane(m_definitionPane.get(), float(1 - split));
}

void JPPanelsPanel::selectPanel(const JPPanel* panel) {
    m_panels->select(panel);
}

double JPPanelsPanel::split() const {
    const std::vector<float> f = m_split->fractions();
    return f.empty() ? 0.5 : f.front();
}

void JPPanelsPanel::refresh() {
    m_panels->refresh();
    m_definition->refresh();
}

} // inline namespace jf
