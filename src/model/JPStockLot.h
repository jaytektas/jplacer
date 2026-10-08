// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include <string>

inline namespace jf {

// One physical lot of a library part (DESIGN.md, Stock lot): a reel, a strip of cut tape, a tray, a tube or a bag;
// who it came from, its date and lot codes, and where it is kept. Stock is "do we have it", not "is it loaded".
// How many it holds is its ledger's (JPLedgerEntry), kept with it as onHand.
class JPStockLot {
public:
    std::string uuid;
    std::string partUuid;    // the library part's
    std::string label;       // what it is called on the shelf ("reel A")
    std::string packaging;   // one of JPPart::kPackagingKinds
    std::string supplier;
    std::string sku;         // the supplier's part number
    std::string dateCode;
    std::string lotCode;
    std::string location;    // where it is kept
    std::string note;
    std::string created;     // JPWhen::now()
    std::string feederId;    // the feeder it is loaded on; empty: on the shelf
    bool        closed = false;   // used up or thrown away: kept for its ledger, not counted as stock
    long long   onHand = 0;       // its ledger's sum (JPStockStore keeps it so)
};

} // inline namespace jf
