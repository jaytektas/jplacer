// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPlacerSettings.h"

#include "model/JPSystemUnits.h"

#include "common/JPlacerLog.h"
#include "common/JPlacerPaths.h"

#include <j/config/Json.h>
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
    // A file there that cannot be read is set aside, not written over with nothing: what it held may be
    // recovered from it.
    std::error_code ec;
    if (std::filesystem::exists(path, ec) && !JJson::tryParseFile(path)) {
        const std::string aside = path + ".unreadable";
        std::filesystem::rename(path, aside, ec);
        JLOGC(JPlacerLog::kSettings, JLogLevel::Error)
            << "settings: " << path << " could not be read; set aside as " << aside << ", starting without them";
    }
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

int JPlacerSettings::theme() {
    return JSettings::instance().get<int>(kTheme, 0);
}

double JPlacerSettings::uiScale() {
    return JSettings::instance().get<double>(kUiScale, 0.0);
}

bool JPlacerSettings::launcher() {
    return JSettings::instance().get<bool>(kLauncher, true);
}

std::string JPlacerSettings::machineCell() {
    return JSettings::instance().get<std::string>(kMachineCell, std::string());
}

std::string JPlacerSettings::cameraStraightKey(const std::string& cameraId) {
    return "camera." + cameraId + ".straight";
}

std::string JPlacerSettings::cameraReticleKey(const std::string& cameraId) {
    return "camera." + cameraId + ".reticle";
}

std::string JPlacerSettings::cameraZoomKey(const std::string& cameraId) {
    return "camera." + cameraId + ".zoomSensitivity";
}

const char* JPlacerSettings::jogDistancesKey() {
    return JPSystemUnits::inches() ? kJogDistancesInches : kJogDistances;
}

std::string JPlacerSettings::cameraRenderingKey(const std::string& cameraId) {
    return "camera." + cameraId + ".renderingQuality";
}

std::string JPlacerSettings::keyFor(const std::string& functionId) {
    return "keys." + functionId;
}

} // inline namespace jf
