// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPLedgerEntry.h"
#include "JPStockLot.h"

#include <j/db/Database.h>

#include <map>
#include <optional>
#include <string>
#include <vector>

inline namespace jf {

// The library's stock (DESIGN.md, Stock lot, Stock ledger): its lots and their ledger, in tables of their own in
// the library's file. Unlike the library's parts (written whole on save), every change is written as it is
// made: a lot received or counted is a fact, kept at once. A lot's on hand is kept with it, worked out again from
// its ledger with each entry. Asked for as needed, never read whole.
class JPStockStore {
public:
    // What a part's ledger says of how many are lost: lost of all taken (used and lost); a measure once any are used.
    struct Attrition {
        long long used = 0;
        long long lost = 0;
        double rate() const { return used + lost > 0 ? double(lost) / double(used + lost) : 0; }
    };

    bool open(const std::string& path, std::string& error);
    bool isOpen() const { return m_db.isOpen(); }

    // A part's lots (by its uuid), oldest first; `closed`: those used up or thrown away too.
    std::vector<JPStockLot> lots(const std::string& partUuid, bool closed) const;
    // One lot; empty uuid when there is none.
    JPStockLot lot(const std::string& uuid) const;
    // A lot's ledger, in order.
    std::vector<JPLedgerEntry> ledger(const std::string& lotUuid) const;
    // How many of each part (by uuid) the open lots hold.
    std::map<std::string, long long> onHandByPart() const;
    Attrition attrition(const std::string& partUuid) const;
    // The attrition set for a part, a share (0.02: 2 %); none set: nullopt.
    std::optional<double> attritionSet(const std::string& partUuid) const;
    bool setAttrition(const std::string& partUuid, std::optional<double> rate, std::string& error);

    // A new lot (given a uuid) and the entry that brought it in (received, or counted for one found on the
    // shelf), in one go.
    bool addLot(JPStockLot& lot, JPLedgerEntry first, std::string& error);
    // A lot's own fields (label, where kept, codes, note, closed); not its on hand.
    bool updateLot(const JPStockLot& lot, std::string& error);
    // An entry added to its lot's ledger (given its id and, when it has none, the time); the lot's on hand
    // worked out again.
    bool addEntry(JPLedgerEntry& entry, std::string& error);

private:
    // The entry written (inside a transaction of the caller's) and its lot's on hand worked out again.
    bool insertEntry(JPLedgerEntry& e);
    bool recount(const std::string& lotUuid);

    mutable JDatabase m_db;
};

} // inline namespace jf
