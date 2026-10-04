// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPOrigin.h"

inline namespace jf {

JPOrigin JPOrigin::fromJson(const JJson& j) {
    JPOrigin o;
    o.libraryId        = j["libraryId"].str();
    o.libraryRevision  = int(j["libraryRevision"].number());
    o.copiedAtRevision = int(j["copiedAtRevision"].number());
    return o;
}

JJson JPOrigin::toJson() const {
    JJson j = JJson::object();
    j["libraryId"]        = libraryId;
    j["libraryRevision"]  = libraryRevision;
    j["copiedAtRevision"] = copiedAtRevision;
    return j;
}

} // inline namespace jf
