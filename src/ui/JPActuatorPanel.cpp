// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPActuatorPanel.h"

#include "JPTextField.h"
#include "JPUiParts.h"

#include <j/core/JScrollArea.h>
#include <j/core/JStyle.h>

inline namespace jf {

JPActuatorPanel::JPActuatorPanel(JSceneGraph& graph, JPCell& cell)
    : JContainer(graph), m_cell(cell) {
    JPUiParts::asPanel(*this);
    if (cell.config().actuators.empty()) {
        add(std::make_unique<JLabel>(graph, "This cell has no actuators."));
        return;
    }
    // Each row a fixed-height container of its own, stacked by the scroll area.
    auto* scroll = add(std::make_unique<JScrollArea>(graph));
    scroll->setVSizePolicy(JSizePolicyMode::Expanding, 1);
    for (const JPActuatorConfig& a : cell.config().actuators) {
        JContainer* r = scroll->addChildWidget(JPUiParts::row(graph));
        r->add(std::make_unique<JLabel>(graph, a.name))->setHSizePolicy(JSizePolicyMode::Expanding, 1);
        const std::string id = a.id;
        if (a.canSwitch()) {
            r->add(JPUiParts::button(graph, "On"))->onClicked.connect([this, id] { m_cell.switchActuator(id, true); });
            r->add(JPUiParts::button(graph, "Off"))->onClicked.connect([this, id] { m_cell.switchActuator(id, false); });
        }
        if (a.canSet()) {
            // A value typed (Return, Tab or leaving it) or Set: sent.
            JPTextField* value = r->add(std::make_unique<JPTextField>(graph));
            value->setText(a.onValue);
            value->onCommitted.connect([this, id](std::string v) { m_cell.setActuator(id, v); });
            r->add(JPUiParts::button(graph, "Set"))->onClicked.connect([this, id, value] { m_cell.setActuator(id, value->text()); });
        }
        if (a.canRead())
            r->add(JPUiParts::button(graph, "Read"))->onClicked.connect([this, id] { m_cell.readActuator(id); });
        m_values[a.id] = r->add(std::make_unique<JLabel>(graph, ""));
    }
    m_watch.on(cell.onActuator, [this](std::string id, bool ok, std::string value) {
        if (const auto it = m_values.find(id); it != m_values.end())
            it->second->setText(ok ? value : "\xE2\x9A\xA0 " + value);
    });
}

} // inline namespace jf
