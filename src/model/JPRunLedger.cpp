// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPRunLedger.h"

#include <filesystem>

inline namespace jf {

bool JPRunLedger::write(JPRunStore& runs, JPStockStore& stock, const std::string& uuid, std::string& error) {
    JPRunStore::Run run = runs.run(uuid);
    if (run.uuid.empty() || run.ledgered) return true;
    if (run.outcome == JPRunStore::Outcome::Open) runs.end(uuid, JPRunStore::Outcome::Interrupted);
    const std::string reference = "run " + run.started + " of " + std::filesystem::path(run.job).filename().string();
    std::vector<JPLedgerEntry> entries;
    for (const auto& [lot, use] : runs.use(uuid)) {
        if (lot.empty() || stock.lot(lot).uuid.empty()) continue;   // a feeder carrying no lot, or one since gone
        JPLedgerEntry e;
        e.lotUuid = lot;
        e.reference = reference;
        if (use.placed > 0) {
            e.kind = JPLedgerEntry::Kind::Used;
            e.quantity = use.placed;
            entries.push_back(e);
        }
        if (use.fed > use.placed) {
            e.kind = JPLedgerEntry::Kind::Lost;
            e.quantity = use.fed - use.placed;
            e.note = "fed and not placed";
            entries.push_back(e);
        }
    }
    if (!entries.empty() && !stock.addEntries(entries, error)) return false;
    return runs.setLedgered(uuid);
}

int JPRunLedger::writeAll(JPRunStore& runs, JPStockStore& stock, std::string& error) {
    int n = 0;
    for (const JPRunStore::Run& r : runs.unledgered()) {
        if (!write(runs, stock, r.uuid, error)) return n;
        ++n;
    }
    return n;
}

} // inline namespace jf
