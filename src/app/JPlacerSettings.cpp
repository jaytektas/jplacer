// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPlacerSettings.h"

#include "common/JPlacerLog.h"
#include "common/JPlacerPaths.h"

#include <j/config/Settings.h>
#include <j/core/Log.h>

#include <filesystem>

inline namespace jf {

std::string JPlacerSettings::defaultPath() {
    const std::string dir = JPlacerPaths::configDir();
    return dir.empty() ? std::string("jplacer.json")
                       : (std::filesystem::path(dir) / "jplacer.json").string();
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

std::string JPlacerSettings::machineCell() {
    return JSettings::instance().get<std::string>(kMachineCell, std::string());
}

} // inline namespace jf
