// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPlacerSettings.h"

#include "model/JPSystemUnits.h"

#include "common/JPlacerLog.h"
#include "common/JPlacerPaths.h"

#include <j/config/Json.h>
#include <j/config/Settings.h>
#include <j/core/VariantJson.h>
#include <j/core/Log.h>

#include <filesystem>
#include <set>

inline namespace jf {

std::string JPlacerSettings::defaultPath() {
    const std::string dir = JPlacerPaths::configDir();
    return dir.empty() ? std::string("jplacer.json")
                       : (std::filesystem::path(dir) / "jplacer.json").string();
}

namespace {
// The file could not be read when loaded: never written this run.
bool s_unreadable = false;
// Settings taken out on purpose this run (back to their defaults): not kept from the file.
std::set<std::string> s_removed;
} // namespace

void JPlacerSettings::load(const std::string& path) {
    std::error_code ec;
    s_unreadable = std::filesystem::exists(path, ec) && !JJson::tryParseFile(path);
    if (s_unreadable)
        JLOGC(JPlacerLog::kSettings, JLogLevel::Error)
            << "settings: " << path << " could not be read; it is left as it is, and nothing is written to it this run";
    JSettings::instance().setPath(path).loadJson();
    JLOGC(JPlacerLog::kSettings, JLogLevel::Info) << "settings: " << path;
}

void JPlacerSettings::remove(const std::string& key) {
    JSettings::instance().remove(key);
    s_removed.insert(key);
}

void JPlacerSettings::save() {
    JSettings& set = JSettings::instance();
    if (s_unreadable) {
        JLOGC(JPlacerLog::kSettings, JLogLevel::Warn) << "settings not saved: " << set.path().string() << " could not be read";
        return;
    }
    // What the file has is kept: a setting this run never read or set (or lost, as a run that started
    // without them would have) stays as it is in the file.
    if (const auto file = JJson::tryParseFile(set.path().string()); file && file->isObject()) {
        const JVariant kept = fromJson(*file);   // held: toMap() refers into it
        for (const auto& [key, value] : kept.toMap())
            if (!set.has(key) && !s_removed.count(key)) set.set(key, value);
    }
    if (!set.saveJson())
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
