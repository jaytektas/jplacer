// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include <cstdint>
#include <string>

inline namespace jf {

// One change to a stock lot (DESIGN.md, Stock ledger): received (an order: how many, what it cost), used by a
// job, lost (a mis-pick, dropped, thrown away), counted (the lot counted: what it holds now) or adjusted (more or
// fewer, by hand). A lot's on hand is its entries in order, so a wrong figure shows why.
class JPLedgerEntry {
public:
    enum class Kind { Received, Used, Lost, Counted, Adjusted };
    static constexpr Kind kKinds[] { Kind::Received, Kind::Used, Kind::Lost, Kind::Counted, Kind::Adjusted };

    int64_t     id = 0;          // its order, as the store gives it
    std::string lotUuid;
    std::string when;            // JPWhen::now()
    Kind        kind = Kind::Received;
    long long   quantity = 0;    // Counted: what the lot holds; Adjusted: more (+) or fewer (-); else how many
    double      cost = 0;        // Received: what they cost, all of them; 0: not given
    std::string reference;       // the order, the job, the run
    std::string note;

    // The lot's on hand after this entry.
    long long apply(long long onHand) const {
        switch (kind) {
            case Kind::Received: return onHand + quantity;
            case Kind::Used:
            case Kind::Lost:     return onHand - quantity;
            case Kind::Counted:  return quantity;
            case Kind::Adjusted: return onHand + quantity;
        }
        return onHand;
    }
    static const char* kindName(Kind k) {
        switch (k) {
            case Kind::Received: return "Received";
            case Kind::Used:     return "Used";
            case Kind::Lost:     return "Lost";
            case Kind::Counted:  return "Counted";
            case Kind::Adjusted: return "Adjusted";
        }
        return "Received";
    }
    static Kind kindFrom(const std::string& s) {
        for (Kind k : kKinds)
            if (s == kindName(k)) return k;
        return Kind::Received;
    }
};

} // inline namespace jf
