// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPlacerApp.h"

#include "common/JPTranslations.h"
#include <j/graphics/FontEngine.h>
#include "model/JPSystemUnits.h"

#include "JPlacerLauncher.h"
#include "common/JPLogLevels.h"
#include "common/JPlacerLog.h"
#include "JPlacerMenuBuilder.h"
#include "JPlacerAppearance.h"
#include "JPlacerPreferencesDialog.h"
#include "JPlacerSettings.h"

#include <j/core/Dialog.h>
#include <j/core/JAiBus.h>
#include <j/core/Log.h>
#include <j/core/MenuSystem.h>

inline namespace jf {

namespace {
constexpr const char* kWindowTitle  = "jplacer";
constexpr uint32_t    kWindowWidth  = 1280;
constexpr uint32_t    kWindowHeight = 800;
// Where jplacer's releases are published, and the variable that points it at a
// pretend one for testing (see JAppUpdater.h).
constexpr const char* kReleasesApi  = "https://api.github.com/repos/jaytektas/jplacer/releases/latest";
constexpr const char* kUpdateUrlEnv = "JPLACER_UPDATE_URL";
}

JPlacerApp::JPlacerApp(std::string settingsPath) {
    JPlacerSettings::load(settingsPath);
    // How much the log says, as last chosen (the console's controls).
    JPLogLevels::fromText(JSettings::instance().get<std::string>(JPlacerSettings::kLogLevels, "info")).apply();
    // The language, as chosen (a change takes effect at the next start): its
    // words, and its letters for the font the window is about to build.
    if (const std::string language = JSettings::instance().get<std::string>(JPlacerSettings::kLanguage, "en"); language != "en") {
        std::string why;
        if (JPTranslations::load(JPTranslations::directory(), language, why)) JFontEngine::addCodepoints(JPTranslations::codepoints());
        else JLOGC(JPlacerLog::kApp, JLogLevel::Warn) << "language " << language << ": " << why;
    }
    // The units lengths are shown in, as chosen (a change takes effect at the next start).
    JPSystemUnits::setUnits(JSettings::instance().get<std::string>(JPlacerSettings::kSystemUnits, "Millimeters") == "Inches"
                                ? JPLengthUnit::Inches
                                : JPLengthUnit::Millimeters);
    JLOGC(JPlacerLog::kApp, JLogLevel::Info) << "starting jplacer " << JPLACER_VERSION;

    m_window = std::make_unique<JAppWindow>(kWindowTitle, kWindowWidth, kWindowHeight);
    if (!m_window->valid()) {
        JLOGC(JPlacerLog::kApp, JLogLevel::Error) << "window / GPU HAL init failed";
        return;
    }
    JPlacerAppearance::applySaved(*m_window);   // before anything is laid out

    m_updater = std::make_unique<JAppUpdater>(
        *m_window, JAppUpdater::JConfig{ "jplacer", JPLACER_VERSION, kReleasesApi, kUpdateUrlEnv,
                                         JPlacerSettings::kUpdatesBeta });

    m_job = std::make_unique<JPlacerJob>(*m_window);
    // The job asked about first, then each changed board (OpenPnP's quit).
    m_window->onCloseRequest = [this] { return m_job->mayClose() && (!m_tabs || m_tabs->mayClose()); };
    m_icons = std::make_unique<JPOpenPnpIcons>(m_window->hal());
    m_machine = std::make_unique<JPlacerMachine>(*m_window, m_app.sceneGraph());
    if (!m_machine->cell() || m_machine->cell()->config().autoLoadMostRecentJob) m_job->openLast();
    m_tabs = std::make_unique<JPlacerOpenPnpTabs>(*m_window, m_app.sceneGraph(), *m_job, *m_machine);
    JMenuManager::instance().setTearOffEnabled(JPlacerSettings::tearOffMenus());
    m_keys = std::make_unique<JPKeyMap>();
    JPlacerMenuBuilder::build(*m_window, m_app.sceneGraph(), *this);
    addJogStepKeys();
    // OpenPnP's Startup event: its scripts run once jplacer is up.
    m_machine->runEvent("Startup");
    // The Jog panel's tooltips say each button's key, as it is now.
    m_machine->keyFor = [this](const std::string& action) {
        const std::string jog = m_keys->keyText("jog." + action);
        return jog.empty() ? m_keys->keyText("machine." + action) : jog;
    };
    m_keys->onChanged = [this] { m_machine->keysChanged(); };
    m_machine->keysChanged();
    // Automation (JF_AI_BUS, jf-busctl): what a widget click cannot reach.
    // "dock:<title>" brings a dock's tab to the front, so its widgets can be
    // driven.
    JAiBus::instance().onAction = [this](uint32_t, const std::string& action) {
        constexpr std::string_view kDock = "dock:";
        if (action.rfind(kDock, 0) != 0) return 0;
        const std::string title = action.substr(kDock.size());
        return m_tabs->showDock(title) || m_machine->showDock(title) ? 1 : -1;
    };
    m_window->setStatusText("jplacer " JPLACER_VERSION);

    // Re-run on every start, so a moved AppImage gets its launcher re-pointed.
    if (JPlacerLauncher::supported() && JPlacerSettings::launcher()) JPlacerLauncher::install();
}

JPlacerApp::~JPlacerApp() {
    JAiBus::instance().onAction = nullptr;   // it calls into this app
    if (m_window) m_window->onCloseRequest = nullptr;
    // The updater records "don't ask about this version again" in JSettings
    // without saving, so the file is written once more on the way out.
    JPlacerSettings::save();
    JLOGC(JPlacerLog::kApp, JLogLevel::Info) << "shut down";
}

bool JPlacerApp::valid() const { return m_window && m_window->valid(); }

int JPlacerApp::run() {
    if (!valid()) return -1;
    // Only a newer version is reported from here: no network on the shop floor is
    // not worth interrupting for every time jplacer opens.
    if (JPlacerSettings::updatesAtStartup()) m_updater->check(false);
    const int rc = m_window->run();
    m_updater->installStaged();
    return rc;
}

void JPlacerApp::openPreferences() {
    m_window->openModal<JPlacerPreferencesDialog>([this] { m_updater->check(true); },
                                                  [this](double scale) { JPlacerAppearance::applyScale(*m_window, scale); },
                                                  *m_keys,
                                                  [this] {
                                                      addJogStepKeys();
                                                      m_machine->jogStepsChanged();
                                                  });
}

void JPlacerApp::addJogStepKeys() {
    // Named by the step, not its place in the row: "1 = 1 mm" stays so when
    // steps are added before it.
    m_keys->removeAll("jog.distance.");
    m_keys->removeAll("jog.speed.");
    const std::vector<double> distances = JPlacerMachine::jogDistances();
    for (size_t i = 0; i < distances.size(); ++i) {
        const std::string step = JPJogPanel::stepText(distances[i]);
        m_keys->add("jog.distance." + step, "Jog Distance", "Distance " + step, {},
                    [this, i] { m_machine->jogAction("distance:" + std::to_string(i)); });
    }
    const std::vector<double> speeds = JPlacerMachine::jogSpeeds();
    for (size_t i = 0; i < speeds.size(); ++i) {
        const std::string step = JPJogPanel::stepText(speeds[i] * 100);
        m_keys->add("jog.speed." + step, "Jog Speed", "Speed " + step + "%", {},
                    [this, i] { m_machine->jogAction("speed:" + std::to_string(i)); });
    }
    m_machine->keysChanged();
}

void JPlacerApp::showAbout() {
    JDialog::message("About jplacer",
        "jplacer " JPLACER_VERSION "\n\n"
        "Pick-and-place machine control.\n\n"
        "Copyright (C) 2026 Jason Roughley\n"
        "This program is free software: you can redistribute it and/or modify it under the "
        "terms of the GNU General Public License, version 3 or later. It comes with ABSOLUTELY "
        "NO WARRANTY.\n\n"
        "https://github.com/jaytektas/jplacer");
}

} // inline namespace jf
