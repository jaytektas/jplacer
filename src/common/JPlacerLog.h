// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include <string>
#include <vector>

inline namespace jf {

// jplacer's log categories. Every JLOGC call names one of these, so a category
// can be turned up from the command line (--trace <category>) on its own, and a
// whole subsystem with a wildcard: --trace 'machine.*'.
//
// Levels, the same everywhere: Error, something failed; Warn, it carried on
// but you should know; Info, what a person did and what came of it; Debug, the
// steps in between; Trace, every byte-level event (each line on the wire).
struct JPlacerLog {
    static constexpr const char* kApp      = "app";        // start, shut down, the cell opened
    static constexpr const char* kSettings = "settings";   // the preferences file
    static constexpr const char* kDesktop  = "desktop";    // the applications-menu entry

    static constexpr const char* kProfiles = "machine.profiles";   // firmware profiles found and loaded
    static constexpr const char* kCell     = "machine.cell";       // connecting a cell, actuators, ports
    static constexpr const char* kDriver   = "machine.driver";     // a controller: identify, settings, failures
    static constexpr const char* kLink     = "machine.link";       // a port: open, close, write failures
    static constexpr const char* kTraffic  = "machine.traffic";    // every command line sent and line received
    static constexpr const char* kStatus   = "machine.status";     // every status report (many a second)

    static constexpr const char* kCamera   = "camera";             // capture devices: opened, mode, failures
    static constexpr const char* kFrames   = "camera.frames";      // every frame (many a second)

    static constexpr const char* kImport   = "import.openpnp";     // reading OpenPnP's files
    static constexpr const char* kBoardImport = "import.board";    // placements read from CAD files

    static constexpr const char* kUi       = "ui";                 // what was clicked, chosen, typed

    // Every category above, in order (for a control that lists them before
    // any has been used).
    static const std::vector<std::string>& all() {
        static const std::vector<std::string> list = { kApp, kSettings, kDesktop, kProfiles, kCell, kDriver, kLink,
                                                       kTraffic, kStatus, kCamera, kFrames, kImport, kBoardImport,
                                                       kUi };
        return list;
    }
};

} // inline namespace jf
