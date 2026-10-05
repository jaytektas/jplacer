// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPlacerAppearanceDialog.h"

#include "JPlacerAppearance.h"
#include "JPlacerSettings.h"
#include "ui/JPTable.h"
#include "ui/JPUiParts.h"

#include <j/config/Settings.h>
#include <j/core/JLabel.h>
#include <j/core/JStyle.h>
#include <j/core/JTextHelper.h>

#include <cmath>

inline namespace jf {

namespace {

double scaleAt(int i) {
    const auto& scales = JPlacerAppearance::scales();
    return i >= 0 && i < int(scales.size()) ? scales[size_t(i)].scale : 0.0;
}

} // namespace

JPlacerAppearanceDialog::JPlacerAppearanceDialog(std::function<void(double)> onScale, JGpuHal& hal, int sx, int sy,
                                                 NativeWinHandleType parent)
    : JDialogWindow("Appearance Settings", kW, kH, hal, sx, sy, parent), m_onScale(std::move(onScale)) {
    JSceneGraph& g = graph();
    const JStyle& st = JStyle::current();
    m_page = std::make_unique<JContainer>(g, 0.f, 0.f);
    JPUiParts::asPanel(*m_page);
    const float widest = std::max(JTextHelper::measureWidth("Theme"), JTextHelper::measureWidth("Font Size"));
    auto labelled = [&](const char* text, std::unique_ptr<JWidget> control) {
        auto row = JPUiParts::row(g);
        auto label = std::make_unique<JLabel>(g, text, 0.f);
        label->setFixedSize(std::ceil(widest) + 2 * st.spacing, st.labelHeight);
        row->add(std::move(label));
        control->setHSizePolicy(JSizePolicyMode::Expanding, 1);
        row->add(std::move(control));
        m_page->add(std::move(row));
    };
    auto theme = std::make_unique<JComboBox>(g, JPlacerAppearance::themes(), 0.f);
    theme->setCurrentIndex(JPlacerSettings::theme());
    m_theme = theme.get();
    labelled("Theme", std::move(theme));
    std::vector<std::string> names;
    int at = 0;
    for (const JPlacerAppearance::Scale& s : JPlacerAppearance::scales()) {
        if (s.scale == JPlacerSettings::uiScale()) at = int(names.size());
        names.push_back(s.name);
    }
    auto scale = std::make_unique<JComboBox>(g, names, 0.f);
    scale->setCurrentIndex(at);
    m_scale = scale.get();
    labelled("Font Size", std::move(scale));
    auto rows = std::make_unique<JCheckBox>(g, "Alternating Rows Style", 0.f);
    rows->setChecked(JPTable::alternateRows());
    m_rows = rows.get();
    m_page->add(std::move(rows));
    add(m_page.get());

    m_buttons = std::make_unique<JDialogButtonBox>(graph());
    m_buttons->addButton("Save", JDialogButtonBox::Role::Accept);
    m_buttons->addButton("Cancel", JDialogButtonBox::Role::Reject);
    m_buttons->addButton("Apply", JDialogButtonBox::Role::Action)->onClicked.connect([this] {
        show(m_theme->currentIndex(), scaleAt(m_scale->currentIndex()), m_rows->isChecked());
    });
    m_buttons->onAccept.connect([this] {
        keep();
        close();
    });
    m_buttons->onReject.connect([this] {
        show(JPlacerSettings::theme(), JPlacerSettings::uiScale(),
             JSettings::instance().get<bool>(JPlacerSettings::kAlternateRows, true));
        close();
    });
    add(m_buttons.get());
}

void JPlacerAppearanceDialog::show(int theme, double scale, bool rows) {
    JPlacerAppearance::applyTheme(theme);
    if (m_onScale) m_onScale(scale);
    JPTable::setAlternateRows(rows);
}

void JPlacerAppearanceDialog::keep() {
    const int theme = m_theme->currentIndex();
    const double scale = scaleAt(m_scale->currentIndex());
    const bool rows = m_rows->isChecked();
    JSettings::instance().set(JPlacerSettings::kTheme, theme);
    JSettings::instance().set(JPlacerSettings::kUiScale, scale);
    JSettings::instance().set(JPlacerSettings::kAlternateRows, rows);
    JPlacerSettings::save();
    show(theme, scale, rows);
}

void JPlacerAppearanceDialog::layout(float w, float h) {
    const JStyle& st = JStyle::current();
    const float pad = 2 * st.fieldPadding, x = pad, cw = w - 2 * pad;
    const float buttonsY = h - pad - st.buttonHeight;
    const float top = contentTop();
    m_page->setBounds({ x, top, cw, std::max(0.f, buttonsY - st.spacing * 2 - top) });
    graph().invalidateNode(m_page->getNodeId(), DirtySelf);
    const JRect b = m_page->bounds();
    graph().computeLayout(m_page->getNodeId(), { b.width, b.width, b.height, b.height });
    m_buttons->setBounds({ x, buttonsY, cw, st.buttonHeight });
}

} // inline namespace jf
