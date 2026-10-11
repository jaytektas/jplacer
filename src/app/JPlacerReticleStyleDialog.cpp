// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPlacerReticleStyleDialog.h"

#include "ui/JPUiParts.h"

#include <j/core/JStyle.h>
#include <j/core/JTextHelper.h>

#include <algorithm>

inline namespace jf {

JPlacerReticleStyleDialog::JPlacerReticleStyleDialog(const std::string& camera, const JPReticle& reticle,
                                                     std::function<void(const JPReticle&)> changed, JGpuHal& hal,
                                                     int sx, int sy, NativeWinHandleType parent)
    : JDialogWindow("Reticle Style: " + camera, kW, kH, hal, sx, sy, parent)
    , m_reticle(reticle)
    , m_changed(std::move(changed)) {
    JSceneGraph& g = graph();
    const JStyle& st = JStyle::current();
    m_content = std::make_unique<JContainer>(g, 0.f, 0.f);
    m_content->setDirection(JFlexDirection::Column)->setAlignItems(JAlignItems::Stretch)->setGap(st.spacing);

    const float labelW = JTextHelper::measureWidth("Footprint") + 2 * st.spacing;
    const float swatchW = JTextHelper::measureWidth("#000000") + st.controlHeight + 4 * st.spacing;   // its hex and its ✕
    // A row: what it styles, its colour, its line width.
    auto row = [&](const std::string& what, const std::string& tip, const std::string& hex, int width, JColorButton*& colour,
                   JSpinBox*& line) {
        auto r = JPUiParts::row(g);
        JLabel* l = r->add(std::make_unique<JLabel>(g, what, labelW));
        l->setTooltip(tip);
        colour = r->add(std::make_unique<JColorButton>(g, swatchW, st.controlHeight));
        colour->setInheritable(true);
        colour->setColorHex(hex);
        colour->setTooltip("Its colour, from the colour chooser; ✕ puts the theme's back (shown as \"(scheme)\")");
        colour->onColorChanged.connect([this](const std::string&) { changedNow(); });
        r->add(std::make_unique<JLabel>(g, "Line", 0.f));
        line = r->add(std::make_unique<JSpinBox>(g, 1, JPReticle::kMostWidth, 0.f));
        line->setValue(width);
        line->setTooltip("How thick its lines are, in the theme's lines (1: the theme's own)");
        line->onValueChanged.connect([this](int) { changedNow(); });
        m_content->add(std::move(r));
    };
    row("Reticle", "The cross through where the camera looks, and the grid, ruler, circle or square drawn to scale",
        m_reticle.colour, m_reticle.lineWidth, m_colour, m_line);
    row("Footprint", "A package's footprint drawn over the picture (a feeder's or a placement's part)",
        m_reticle.footprintColour, m_reticle.footprintWidth, m_footprintColour, m_footprintLine);
    add(m_content.get());

    m_buttons = std::make_unique<JDialogButtonBox>(g);
    m_buttons->addButton("Close", JDialogButtonBox::Role::Reject)->setTooltip("Each change is already kept");
    m_buttons->onReject.connect([this] { close(); });
    add(m_buttons.get());
}

void JPlacerReticleStyleDialog::changedNow() {
    m_reticle.colour = m_colour->colorHex();
    m_reticle.footprintColour = m_footprintColour->colorHex();
    m_reticle.lineWidth = std::clamp(m_line->value(), 1, JPReticle::kMostWidth);
    m_reticle.footprintWidth = std::clamp(m_footprintLine->value(), 1, JPReticle::kMostWidth);
    if (m_changed) m_changed(m_reticle);
}

void JPlacerReticleStyleDialog::layout(float w, float h) {
    const JStyle& st = JStyle::current();
    const float pad = st.spacing * 2, cw = w - 2 * pad;
    const float buttonsY = h - pad - st.buttonHeight;
    const float top = contentTop() + pad;
    m_content->setBounds({ pad, top, cw, std::max(0.f, buttonsY - st.spacing * 2 - top) });
    graph().invalidateNode(m_content->getNodeId(), DirtySelf);
    const JRect b = m_content->bounds();
    graph().computeLayout(m_content->getNodeId(), { b.width, b.width, b.height, b.height });
    m_buttons->setBounds({ pad, buttonsY, cw, st.buttonHeight });
}

} // inline namespace jf
