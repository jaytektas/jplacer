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
// Kept in a JSON file of their own beside the configuration, with each
// file's columns as they were last imported (last): the next file with the
// same columns is read as that one was, saved as a profile or not.
class JPMappingProfiles {
public:
    static constexpr const char* kFile = "import-profiles.json";
    static constexpr const char* kLast = "As last imported";   // the name a remembered mapping goes by

    struct Profile {
        std::string                        name;
        JPImportSource::Role               role = JPImportSource::Role::Cpl;
        std::map<std::string, std::string> columns;   // header, as the file has it → field (JPImportField::Target::text)
        JPLengthUnit                       units = JPLengthUnit::Millimeters;
    };

    std::vector<Profile> profiles;
    std::vector<Profile> last;   // one a role and header, each called kLast

    // Read from / written to `path`; reading one that is not there is no profiles, not a fault.
    bool load(const std::string& path, std::string& error);
    bool save(const std::string& path, std::string& error) const;

    // The profile for a file of this header and role, or null.
    const Profile* best(const std::vector<std::string>& header, JPImportSource::Role role) const;
    // A source mapped by a profile: each column the profile names, as it names it (a field since removed:
    // nothing); the rest guessed.
    static void apply(const Profile& p, JPImportSource& s, const JPFields& fields);
    // A source's mapping as a profile called `name`.
    static Profile from(const JPImportSource& s, const std::string& name);
    // Kept, in place of one of the same name.
    void put(const Profile& p);
    // The mapping a file of exactly this header and role was last imported by, or null; a source's kept as
    // that, in place of the one before.
    const Profile* lastFor(const std::vector<std::string>& header, JPImportSource::Role role) const;
    void remember(const JPImportSource& s);
};

} // inline namespace jf
