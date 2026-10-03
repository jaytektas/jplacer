// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include <j/app/JAppWindow.h>

#include <string>
#include <vector>

inline namespace jf {

// How jplacer looks: its theme, dark or light (or as the desktop is set),
// and its interface scale, how big the whole interface is (as the screen
// asks, or a size chosen). Kept in the settings (JPlacerSettings::kTheme,
// kUiScale), applied at start and the moment Preferences changes them.
class JPlacerAppearance {
public:
    // The theme choices, in JPlacerSettings::kTheme's numbering.
    static const std::vector<std::string>& themes();
    // The interface scales offered: a name and a scale (0: as the screen asks).
    struct Scale {
        std::string name;
        double      scale;
    };
    static const std::vector<Scale>& scales();

    // The saved theme and scale, applied; the theme first, as installing a
    // theme puts the scale back over it.
    static void applySaved(JAppWindow& window);
    static void applyTheme(int choice);
    static void applyScale(JAppWindow& window, double scale);

private:
    // Whether the desktop asks for dark (GNOME's colour scheme, Windows' app
    // mode); dark when it cannot be told.
    static bool desktopPrefersDark();
};

} // inline namespace jf
