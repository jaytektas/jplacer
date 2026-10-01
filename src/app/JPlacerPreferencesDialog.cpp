// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPlacerPreferencesDialog.h"

#include "JPlacerSettings.h"

#include <j/config/Settings.h>
#include <j/core/JStyle.h>
#include <j/core/MainThreadDispatcher.h>

#include <algorithm>

inline namespace jf {

JPlacerPreferencesDialog::JPlacerPreferencesDialog(std::function<void()> onCheckNow,
                                                   JGpuHal& hal, int sx, int sy,
                                                   NativeWinHandleType parent)
    : JDialogWindow("Preferences", kW, kH, hal, sx, sy, parent)
    , m_onCheckNow(std::move(onCheckNow)) {
    const float cw = kW - 2 * pad();

    m_heading = std::make_unique<JLabel>(graph(), "Updates", cw);

    m_atStartup = std::make_unique<JCheckBox>(graph(), "Check for updates when jplacer opens", cw);
    m_atStartup->setChecked(JPlacerSettings::updatesAtStartup());
    m_atStartup->onStateChanged.connect([](bool on) {
        JSettings::instance().set(JPlacerSettings::kUpdatesAtStartup, on);
        JPlacerSettings::save();
    });

    m_beta = std::make_unique<JCheckBox>(graph(), "Include beta versions", cw);
    m_beta->setChecked(JPlacerSettings::updatesBeta());
    m_beta->onStateChanged.connect([](bool on) {
        JSettings::instance().set(JPlacerSettings::kUpdatesBeta, on);
        JPlacerSettings::save();
    });

    m_betaNote = std::make_unique<JLabel>(graph(),
        "Beta versions get new features first and have had less testing.", cw);
    m_betaNote->setWordWrap(true);

    m_buttons = std::make_unique<JDialogButtonBox>(graph());
    m_buttons->addButton("Check Now", JDialogButtonBox::Role::Action)->onClicked.connect([this] {
        close();
        // Posted, so the check starts once this window is off the modal stack and
        // whatever it finds is shown over the main window rather than behind it.
        JMainThreadDispatcher::instance().post(m_onCheckNow);
    });
    m_buttons->addButton("Close", JDialogButtonBox::Role::Accept);
    m_buttons->onAccept.connect([this] { close(); });

    add(m_heading.get());
    add(m_atStartup.get());
    add(m_beta.get());
    add(m_betaNote.get());
    add(m_buttons.get());
}

float JPlacerPreferencesDialog::pad() { return 2 * JStyle::current().fieldPadding; }

void JPlacerPreferencesDialog::layout(float w, float h) {
    const JStyle& st = JStyle::current();
    const float x = pad(), cw = w - 2 * pad(), gap = 2 * st.spacing;
    const float noteH = std::max(st.labelHeight, m_betaNote->heightFor(cw));

    float y = contentTop();
    m_heading  ->setBounds({ x, y, cw, st.labelHeight }); y += st.labelHeight + gap;
    m_atStartup->setBounds({ x, y, cw, st.checkHeight }); y += st.checkHeight + gap;
    m_beta     ->setBounds({ x, y, cw, st.checkHeight }); y += st.checkHeight + st.spacing;
    m_betaNote ->setBounds({ x, y, cw, noteH });

    m_buttons->setBounds({ x, h - pad() - st.buttonHeight, cw, st.buttonHeight });
}

} // inline namespace jf
