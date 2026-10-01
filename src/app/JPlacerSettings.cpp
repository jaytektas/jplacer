// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPlacerSettings.h"

#include "JPlacerLog.h"

#include <j/config/Settings.h>
#include <j/core/Log.h>

#include <cstdlib>
#include <filesystem>

inline namespace jf {

std::string JPlacerSettings::defaultPath() {
    namespace fs = std::filesystem;
#if defined(_WIN32)
    if (const char* appData = std::getenv("APPDATA"))
        return (fs::path(appData) / "jplacer" / "jplacer.json").string();
#else
    if (const char* xdg = std::getenv("XDG_CONFIG_HOME"); xdg && *xdg)
        return (fs::path(xdg) / "jplacer" / "jplacer.json").string();
    if (const char* home = std::getenv("HOME"))
        return (fs::path(home) / ".config" / "jplacer" / "jplacer.json").string();
#endif
    return "jplacer.json";
}

void JPlacerSettings::load(const std::string& path) {
    JSettings::instance().setPath(path).loadJson();
    JLOGC(JPlacerLog::kSettings, JLogLevel::Info) << "settings: " << path;
}

void JPlacerSettings::save() {
    if (!JSettings::instance().saveJson())
        JLOGC(JPlacerLog::kSettings, JLogLevel::Error)
            << "settings not saved to " << JSettings::instance().path().string();
}

bool JPlacerSettings::updatesBeta() {
    return JSettings::instance().get<bool>(kUpdatesBeta, false);
}

bool JPlacerSettings::updatesAtStartup() {
    return JSettings::instance().get<bool>(kUpdatesAtStartup, true);
}

bool JPlacerSettings::tearOffMenus() {
    return JSettings::instance().get<bool>(kTearOffMenus, false);
}

bool JPlacerSettings::launcher() {
    return JSettings::instance().get<bool>(kLauncher, true);
}

} // inline namespace jf
