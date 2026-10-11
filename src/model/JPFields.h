// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPFieldSet.h"

#include <string>
#include <vector>

inline namespace jf {

// Every kind's fields (JPFieldSet), kept in the configuration folder's fields.json: what was added, and the
// built-ins renamed, given matches or removed. Counts its changes (version), so what shows them can tell.
class JPFields {
public:
    static constexpr const char* kFile = "fields.json";
    static constexpr const char* kFormat = "jplacer-fields";

    JPFields();

    JPFieldSet&       of(JPFieldSet::Of of);
    const JPFieldSet& of(JPFieldSet::Of of) const;

    // Read from `path` (none there: the built-ins alone, not a fault); written there whole.
    bool load(const std::string& path, std::string& error);
    bool save(const std::string& path, std::string& error) const;

    // Bumped by every change (changed()), so a table can tell its columns are to be made again.
    unsigned version() const { return m_version; }
    void     changed() { ++m_version; }

private:
    std::vector<JPFieldSet> m_sets;   // in JPFieldSet::kinds()'s order
    unsigned                m_version = 1;
};

} // inline namespace jf
