// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

inline namespace jf {

// jplacer's log categories. Every JLOGC call names one of these, so a category
// can be turned up from the command line (--trace <category>) on its own.
struct JPlacerLog {
    static constexpr const char* kApp      = "app";
    static constexpr const char* kSettings = "settings";
    static constexpr const char* kDesktop  = "desktop";
    static constexpr const char* kDriver   = "driver";
    static constexpr const char* kProfiles = "profiles";
};

} // inline namespace jf
