// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPBoard.h"
#include "JPPlacementsHolderLocation.h"

#include <map>
#include <memory>
#include <string>

inline namespace jf {

// A board on a panel or in a job, as OpenPnP's BoardLocation ("Brd1").
class JPBoardLocation : public JPPlacementsHolderLocation {
public:
    static constexpr const char* kIdPrefix = "Brd";

    JPBoardLocation() = default;
    Kind kind() const override { return Kind::Board; }
    std::unique_ptr<JPPlacementsHolderLocation> instance() const override;

    JPBoard*       board() { return static_cast<JPBoard*>(holder.get()); }
    const JPBoard* board() const { return static_cast<const JPBoard*>(holder.get()); }

    // An older job's placed marks (by placement id), turned into the job's
    // map when it is converted.
    std::map<std::string, bool> legacyPlaced;
    // The revision of its board (JPBoard::revisionLabel) the job was saved with, shown when the job is opened;
    // empty: the board kept no revisions. A job's file keeps the one its board shows.
    std::string revision;

private:
    JPBoardLocation(const JPBoardLocation& o) : JPPlacementsHolderLocation(o), revision(o.revision) {}
};

} // inline namespace jf
