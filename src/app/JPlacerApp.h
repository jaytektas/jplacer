// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include <j/app/JAppUpdater.h>
#include <j/app/JAppWindow.h>
#include <j/core/GenesisComponents.h>

#include "JPKeyMap.h"
#include "JPlacerJob.h"
#include "JPlacerMachine.h"
#include "JPlacerLibraryDock.h"
#include "JPlacerParts.h"

#include <memory>
#include <string>

inline namespace jf {

// The application: the window, the updater, and the wiring between them.
//
// A wiring object, not a home for features. The menu tree lives in
// JPlacerMenuBuilder, preferences in JPlacerPreferencesDialog; as the machine,
// job and vision subsystems arrive they get classes of their own and this one
// connects them.
class JPlacerApp {
public:
    explicit JPlacerApp(std::string settingsPath);
    ~JPlacerApp();

    JPlacerApp(const JPlacerApp&)            = delete;
    JPlacerApp& operator=(const JPlacerApp&) = delete;

    bool valid() const;
    int  run();

    void openPreferences();
    void showAbout();

    JAppWindow&  window()  { return *m_window; }
    JAppUpdater& updater() { return *m_updater; }
    JPlacerMachine& machine() { return *m_machine; }
    JPlacerJob& job() { return *m_job; }
    JPlacerParts& parts() { return *m_parts; }
    JPlacerLibraryDock& library() { return *m_library; }
    JPKeyMap& keys() { return *m_keys; }

private:
    JGuiApplication              m_app;
    std::unique_ptr<JAppWindow>  m_window;
    // jplacer's own updates, from its GitHub releases: checked as it opens (unless
    // turned off in Preferences), on Help > Check for Updates, and installed as
    // the window closes.
    std::unique_ptr<JAppUpdater> m_updater;
    // The open job and the parts library; outlives the machine, whose Board
    // panel shows the job's board.
    std::unique_ptr<JPlacerJob> m_job;
    // The open cell and its panel; before the window in destruction order.
    std::unique_ptr<JPlacerMachine> m_machine;
    // The job's Parts dock, in the machine's layout: gone before the machine.
    std::unique_ptr<JPlacerParts> m_parts;
    std::unique_ptr<JPlacerLibraryDock> m_library;
    // Every function a key can be given; after the machine, so gone first.
    std::unique_ptr<JPKeyMap> m_keys;

    // A key for each jog step (Preferences > Jog), made again when they change.
    void addJogStepKeys();
};

} // inline namespace jf
