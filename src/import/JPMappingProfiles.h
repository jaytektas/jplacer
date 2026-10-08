// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPImportSource.h"

#include <map>
#include <string>
#include <vector>

inline namespace jf {

// The column mappings confirmed once and named (DESIGN.md, Mapping
// profile): "KiCad 8 .pos", "JLCPCB BOM". A file whose header has every
// column a profile maps is mapped by it (the profile mapping most of them,
// where several fit), so the next file from the same tool maps itself.
// Kept in a JSON file of their own beside the configuration.
class JPMappingProfiles {
public:
    static constexpr const char* kFile = "import-profiles.json";

    struct Profile {
        std::string                        name;
        JPImportSource::Role               role = JPImportSource::Role::Cpl;
        std::map<std::string, std::string> columns;   // header, as the file has it → field key
        JPLengthUnit                       units = JPLengthUnit::Millimeters;
    };

    std::vector<Profile> profiles;

    // Read from / written to `path`; reading one that is not there is no profiles, not a fault.
    bool load(const std::string& path, std::string& error);
    bool save(const std::string& path, std::string& error) const;

    // The profile for a file of this header and role, or null.
    const Profile* best(const std::vector<std::string>& header, JPImportSource::Role role) const;
    // A source mapped by a profile: each column the profile names, as it names it; the rest guessed.
    static void apply(const Profile& p, JPImportSource& s);
    // A source's mapping as a profile called `name`.
    static Profile from(const JPImportSource& s, const std::string& name);
    // Kept, in place of one of the same name.
    void put(const Profile& p);
};

} // inline namespace jf
