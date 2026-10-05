// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include <j/core/Log.h>

#include <string>

inline namespace jf {

// A log entry as jplacer writes it, in the Log tab and its log file:
// "2026-10-05 12:00:00.123 job INFO: …", stamped now.
class JPLogLine {
public:
    static std::string text(JLogLevel level, const std::string& category, const std::string& message);
    // OpenPnP's (tinylog's) names for the levels.
    static const char* levelName(JLogLevel level);
};

} // inline namespace jf
