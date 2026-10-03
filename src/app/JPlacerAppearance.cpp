// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPlacerAppearance.h"

#include "JPlacerSettings.h"

#include "common/JPlacerLog.h"

#include <j/core/JStyle.h>
#include <j/core/Log.h>

#include <cstdio>

#if defined(_WIN32)
#include <windows.h>
#endif

inline namespace jf {

const std::vector<std::string>& JPlacerAppearance::themes() {
    static const std::vector<std::string> kThemes{ "Dark", "Light", "As the desktop is set" };
    return kThemes;
}

const std::vector<JPlacerAppearance::Scale>& JPlacerAppearance::scales() {
    static const std::vector<Scale> kScales{ { "As the screen asks", 0.0 }, { "100 %", 1.0 }, { "125 %", 1.25 },
                                             { "150 %", 1.5 }, { "175 %", 1.75 }, { "200 %", 2.0 } };
    return kScales;
}

void JPlacerAppearance::applySaved(JAppWindow& window) {
    applyTheme(JPlacerSettings::theme());
    applyScale(window, JPlacerSettings::uiScale());
}

void JPlacerAppearance::applyTheme(int choice) {
    const bool dark = choice == 0 || (choice == 2 && desktopPrefersDark());
    JStyle::apply(dark ? JStyle::dark() : JStyle::light());
    JLOGC(JPlacerLog::kUi, JLogLevel::Info) << "theme " << (dark ? "dark" : "light");
}

void JPlacerAppearance::applyScale(JAppWindow& window, double scale) {
    window.setUiScale(scale > 0 ? float(scale) : window.screenScale());
    JLOGC(JPlacerLog::kUi, JLogLevel::Info) << "interface scale " << JStyle::uiScale()
                                           << (scale > 0 ? " (chosen)" : " (as the screen asks)");
}

bool JPlacerAppearance::desktopPrefersDark() {
#if defined(_WIN32)
    // Windows' "app mode": AppsUseLightTheme 0 is dark.
    DWORD light = 0, size = sizeof light;
    if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                     L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &light, &size) != ERROR_SUCCESS)
        return true;
    return light == 0;
#else
    FILE* p = popen("gsettings get org.gnome.desktop.interface color-scheme 2>/dev/null", "r");
    if (!p) return true;
    char buf[128] = {};
    const bool read = std::fgets(buf, sizeof buf, p) != nullptr;
    pclose(p);
    const std::string s = read ? buf : "";
    if (s.find("dark") != std::string::npos) return true;
    return s.find("light") == std::string::npos && s.find("default") == std::string::npos;
#endif
}

} // inline namespace jf
