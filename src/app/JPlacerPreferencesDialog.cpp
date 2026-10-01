// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPlacerPreferencesDialog.h"

#include "JPlacerLauncher.h"
#include "JPlacerSettings.h"

#include <j/config/Settings.h>
#include <j/core/JStyle.h>
#include <j/core/MainThreadDispatcher.h>
#include <j/core/MenuSystem.h>

#include <algorithm>
#include <cmath>

inline namespace jf {

namespace {

void store(const char* key, bool on) {
    JSettings::instance().set(key, on);
    JPlacerSettings::save();
}

} // namespace

JPlacerPreferencesDialog::JPlacerPreferencesDialog(std::function<void()> onCheckNow,
                                                   JGpuHal& hal, int sx, int sy,
                                                   NativeWinHandleType parent)
    : JDialogWindow("Preferences", kW, kH, hal, sx, sy, parent)
    , m_onCheckNow(std::move(onCheckNow)) {
    const float cw = kW - 2 * pad();

    m_general = std::make_unique<JLabel>(graph(), "General", cw);
    add(m_general.get());

    m_tearOff = std::make_unique<JCheckBox>(graph(), "Tear-off menus (drag a menu off into its own window)", cw);
    m_tearOff->setChecked(JPlacerSettings::tearOffMenus());
    m_tearOff->onStateChanged.connect([](bool on) {
        JMenuManager::instance().setTearOffEnabled(on);   // read each time a menu opens
        store(JPlacerSettings::kTearOffMenus, on);
    });
    add(m_tearOff.get());

    if (JPlacerLauncher::supported()) {
        m_launcher = std::make_unique<JCheckBox>(graph(), "Show jplacer in the applications menu", cw);
        m_launcher->setChecked(JPlacerSettings::launcher());
        m_launcher->onStateChanged.connect([](bool on) {
            if (on) JPlacerLauncher::install(); else JPlacerLauncher::remove();
            store(JPlacerSettings::kLauncher, on);
        });
        add(m_launcher.get());
    }

    m_updates = std::make_unique<JLabel>(graph(), "Updates", cw);
    add(m_updates.get());

    m_atStartup = std::make_unique<JCheckBox>(graph(), "Check for updates when jplacer opens", cw);
    m_atStartup->setChecked(JPlacerSettings::updatesAtStartup());
    m_atStartup->onStateChanged.connect([](bool on) { store(JPlacerSettings::kUpdatesAtStartup, on); });
    add(m_atStartup.get());

    m_beta = std::make_unique<JCheckBox>(graph(), "Include beta versions", cw);
    m_beta->setChecked(JPlacerSettings::updatesBeta());
    m_beta->onStateChanged.connect([](bool on) { store(JPlacerSettings::kUpdatesBeta, on); });
    add(m_beta.get());

    m_betaNote = std::make_unique<JLabel>(graph(),
        "Beta versions get new features first and have had less testing.", cw);
    m_betaNote->setWordWrap(true);
    add(m_betaNote.get());

    m_buttons = std::make_unique<JDialogButtonBox>(graph());
    m_buttons->addButton("Check Now", JDialogButtonBox::Role::Action)->onClicked.connect([this] {
        close();
        // Posted, so the check starts once this window is off the modal stack and
        // whatever it finds is shown over the main window rather than behind it.
        JMainThreadDispatcher::instance().post(m_onCheckNow);
    });
    m_buttons->addButton("Close", JDialogButtonBox::Role::Accept);
    m_buttons->onAccept.connect([this] { close(); });
    add(m_buttons.get());
}

float JPlacerPreferencesDialog::pad() { return 2 * JStyle::current().fieldPadding; }

void JPlacerPreferencesDialog::layout(float w, float h) {
    const JStyle& st = JStyle::current();
    const float x = pad(), cw = w - 2 * pad(), gap = 2 * st.spacing, section = 4 * st.spacing;
    const float noteH = std::max(st.labelHeight, m_betaNote->heightFor(cw));

    float y = contentTop();
    auto row = [&](JWidget* wgt, float rowH, float after) {
        wgt->setBounds({ x, y, cw, rowH });
        y += rowH + after;
    };
    row(m_general.get(),   st.labelHeight, gap);
    row(m_tearOff.get(),   st.checkHeight, gap);
    if (m_launcher) row(m_launcher.get(), st.checkHeight, gap);
    y += section - gap;
    row(m_updates.get(),   st.labelHeight, gap);
    row(m_atStartup.get(), st.checkHeight, gap);
    row(m_beta.get(),      st.checkHeight, st.spacing);
    row(m_betaNote.get(),  noteH,          section);

    // The window is as tall as its rows: asked once per change, not per frame.
    const uint32_t wantH = static_cast<uint32_t>(std::ceil(y + st.buttonHeight + pad()));
    if (wantH != m_askedH) { m_askedH = wantH; window().setSize(kW, wantH); }

    m_buttons->setBounds({ x, h - pad() - st.buttonHeight, cw, st.buttonHeight });
}

} // inline namespace jf
