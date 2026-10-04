// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include <j/config/Json.h>

#include <string>

inline namespace jf {

// Where a job's copy of a part, package or footprint came from: the library
// entry's id and the revision it had when copied, and the copy's own
// revision then. Empty id: made in the job. The job tells the person when
// the library's revision has moved on, or the copy's own has (changed in the
// job).
struct JPOrigin {
    std::string libraryId;
    int         libraryRevision = 0;
    int         copiedAtRevision = 0;   // the copy's revision when it was made

    bool fromLibrary() const { return !libraryId.empty(); }
    static JPOrigin fromJson(const JJson& j);
    JJson toJson() const;
    bool operator==(const JPOrigin&) const = default;
};

} // inline namespace jf
