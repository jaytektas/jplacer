// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPAxesPanel.h"

#include "JPUiParts.h"

#include <j/core/JScrollArea.h>
#include <j/core/JStyle.h>

inline namespace jf {

JPAxesPanel::JPAxesPanel(JSceneGraph& graph, JPCell& cell)
    : JContainer(graph) {
    JPUiParts::asPanel(*this);
    auto* scroll = add(std::make_unique<JScrollArea>(graph));
    scroll->setVSizePolicy(JSizePolicyMode::Expanding, 1);
    const JStyle& st = JStyle::current();
    const auto positions = cell.positions();
    for (const JPAxisConfig& a : cell.config().axes) {
        auto r = std::make_unique<JContainer>(graph, 0.f, st.labelHeight);
        r->setDirection(JFlexDirection::JRow)->setGap(st.spacing)->setAlignItems(JAlignItems::Center);
        std::string kind = JPAxisConfig::kindName(a.kind);
        if (a.kind == JPAxisConfig::Kind::Controller) {
            const JPDriverConfig* d = cell.config().driver(a.driverId);
            kind = (d ? d->name : a.driverId) + " " + a.letter;
        } else if (a.kind == JPAxisConfig::Kind::Mapped) {
            const JPAxisConfig* in = cell.config().axis(a.inputAxisId);
            kind = "follows " + (in ? in->name : a.inputAxisId);
        }
        r->add(std::make_unique<JLabel>(graph, a.name))->setHSizePolicy(JSizePolicyMode::Expanding, 1);
        r->add(std::make_unique<JLabel>(graph, kind))->setHSizePolicy(JSizePolicyMode::Expanding, 1);
        const auto p = positions.find(a.id);
        m_values[a.id] = r->add(std::make_unique<JLabel>(graph, p == positions.end() ? "" : JPUiParts::coordinate(p->second)));
        scroll->addChildWidget(std::move(r));
    }
    m_watch.on(cell.onPositions, [this](std::map<std::string, double> positions) {
        for (const auto& [id, v] : positions)
            if (const auto it = m_values.find(id); it != m_values.end()) it->second->setText(JPUiParts::coordinate(v));
    });
}

} // inline namespace jf
