// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include <string>

inline namespace jf {

// A copy of what a run starts from, taken before it reads or writes anything: the
// settings file and every cell (machine) in the cells folder, into
// backups/<date-time>/ beside them, the newest `keep` copies kept and the older
// ones removed. What was there is copied, never changed.
class JPBackups {
public:
    static constexpr int kKeep = 20;

    // `configDir`: where the cells folder and backups are; `settingsFile`: the
    // settings file (copied when there). False, and why, when nothing could be
    // copied; `where` the folder written.
    static bool take(const std::string& configDir, const std::string& settingsFile, int keep, std::string& where,
                     std::string& why);
};

} // inline namespace jf
