// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include <string>

inline namespace jf {

// Where jplacer's files are: the directory the executable runs from (bundled
// data such as the manual and firmware profiles sits beside it in the
// AppImage, or in the repository when run from build/), and the per-user
// configuration directory (preferences, cells, and OpenPnP's configuration
// files: parts, packages, boards and panels).
class JPlacerPaths {
public:
    // The executable's directory; empty if the system will not say.
    static std::string exeDir();

    // ~/.config/jplacer (or $XDG_CONFIG_HOME/jplacer, %APPDATA%\jplacer).
    // Empty when no home directory is known.
    static std::string configDir();
};

} // inline namespace jf
