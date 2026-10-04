// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPLocation.h"
#include "JPPlacement.h"
#include "JPProfile.h"

#include <memory>
#include <optional>
#include <string>
#include <vector>

inline namespace jf {

// What boards and panels share, as OpenPnP's PlacementsHolder: a name, the
// dimensions, the placements, an outline, and the file it is kept in.
//
// A holder read from a file is the DEFINITION (JPConfiguration keeps it);
// each place it is used (a job's board, a board on a panel) holds a copy of
// its own, an instance, whose definition() is the one read. Edits to the
// definition are saved to its file; an instance keeps only what a job sets
// on it (enabled, error handling), as OpenPnP does.
class JPPlacementsHolder {
public:
    enum class Kind { Board, Panel };

    virtual ~JPPlacementsHolder() = default;
    virtual Kind kind() const = 0;
    // An instance of this one: a copy whose definition is this one's.
    virtual std::shared_ptr<JPPlacementsHolder> instance() const = 0;

    std::optional<std::string> name;
    JPLocation                 dimensions;
    std::vector<JPPlacement>   placements;
    std::optional<JPProfile>   profile;
    std::string                file;    // its file's full path; empty: none
    bool                       dirty = false;

    // The definition this is an instance of; itself, when it is one.
    const JPPlacementsHolder* definition() const { return m_definition ? m_definition : this; }
    bool isDefinition() const { return m_definition == nullptr; }
    void makeDefinition() { m_definition = nullptr; }

    // The outline: the profile, else a rectangle of the dimensions.
    JPProfile outline() const;
    JPPlacement*       find(const std::string& id);
    const JPPlacement* find(const std::string& id) const;
    // The dimensions, with Z and the rotation 0 (as OpenPnP keeps them).
    void setDimensions(const JPLocation& l) { dimensions = l.derive(std::nullopt, std::nullopt, 0.0, 0.0); }

    // A new id: `prefix` and the first number from 1 not in `taken`.
    template <class F>
    static std::string createId(const std::string& prefix, F taken) {
        for (int i = 1;; ++i)
            if (!taken(prefix + std::to_string(i))) return prefix + std::to_string(i);
    }

protected:
    JPPlacementsHolder() = default;
    JPPlacementsHolder(const JPPlacementsHolder&) = default;
    JPPlacementsHolder& operator=(const JPPlacementsHolder&) = default;

    const JPPlacementsHolder* m_definition = nullptr;
};

} // inline namespace jf
