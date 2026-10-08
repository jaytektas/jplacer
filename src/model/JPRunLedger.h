// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPRunStore.h"
#include "JPStockStore.h"

#include <string>

inline namespace jf {

// A run's parts written to the stock's ledger (DESIGN.md, Stock ledger): for each lot its feeders fed from, the
// parts placed as Used and the rest fed as Lost (mis-picked, dropped, discarded), one entry of each, its
// reference the run. Each run is written once; a run left open (jplacer closed or crashed while it ran) is
// ended as interrupted and written from what it recorded.
class JPRunLedger {
public:
    // The run `uuid` written (a run ended; open, it is ended as interrupted first). False, and why, when the
    // ledger could not be written (the run is left to be written again).
    static bool write(JPRunStore& runs, JPStockStore& stock, const std::string& uuid, std::string& error);
    // Every run not written yet (those left open ended as interrupted); how many were written.
    static int writeAll(JPRunStore& runs, JPStockStore& stock, std::string& error);
};

} // inline namespace jf
