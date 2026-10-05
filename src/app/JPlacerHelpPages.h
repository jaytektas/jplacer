// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include <string>

inline namespace jf {

// The user manual, opened in the person's own browser.
//
// A browser is the right reader for a manual: it scrolls, searches, zooms,
// prints and can sit beside jplacer on another screen. The manual is the
// MkDocs site in manual/ (manual/STANDARD.md), built by manual/tools/build.sh.
// The AppImage carries it beside the executable (usr/bin/manual); a jplacer run
// from build/ finds the copy the build script made in the repository.
//
// SERVED OVER HTTP, not opened as a file: a browser sandboxed as a snap
// (Ubuntu's default Firefox and Chromium) cannot read inside an AppImage's
// mount, but every browser can read http://127.0.0.1. JLocalWebServer is
// loopback-only and started the first time a page is opened.
class JPlacerHelpPages {
public:
    // Open the manual's front page / its What's New page. False with `error`
    // saying why when there is no manual or no browser could be asked.
    static bool openManual(std::string& error);
    static bool openWhatsNew(std::string& error);
    // OpenPnP's Quick Start and Setup and Calibration: the manual's Getting Started and Machine Setup pages.
    static bool openQuickStart(std::string& error);
    static bool openSetupAndCalibration(std::string& error);

private:
    static bool openPage(const std::string& page, std::string& error);
    static std::string manualDir();
};

} // inline namespace jf
