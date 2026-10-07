// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include <string>

inline namespace jf {

// When something was done, as calibrations keep it ("2026-10-07 14:20:45", local time), and how long ago that is,
// in words a person reads at a glance: "just now", "3 hours ago", "12 days ago", "2 months ago".
class JPWhen {
public:
    // Now, as it is kept.
    static std::string now();
    // `when` with how long ago in brackets ("2026-10-07 14:20 (3 hours ago)"); a `when` not in that form
    // ("measured in OpenPnP") as it is; empty: "never".
    static std::string withAgo(const std::string& when);
    // How long ago `when` was ("3 hours ago"); empty when it cannot be read.
    static std::string ago(const std::string& when);
};

} // inline namespace jf
