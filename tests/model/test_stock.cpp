// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// The library's stock (DESIGN.md, Stock lot, Stock ledger): lots received into the library's file, their ledger
// (used, lost, counted, adjusted) giving their on hand, attrition measured from it or set, a closed lot no longer
// stock, all kept as made; and a job's shortages: its placements left to place against the stock, with attrition.
// Tests check with assert(); a Release build must not compile it away.
#undef NDEBUG
#include <cassert>

#include "model/JPBoardLocation.h"
#include "model/JPConfiguration.h"
#include "model/JPJob.h"
#include "model/JPShortages.h"

#include <cmath>
#include <filesystem>

using namespace jf;
namespace fs = std::filesystem;

int main() {
    const fs::path dir = fs::temp_directory_path() / "jplacer-test-stock";
    fs::remove_all(dir);
    fs::create_directories(dir);
    std::string error;
    std::string reelA, partUuid, capUuid;
    {
        JPConfiguration c(dir.string());
        std::vector<std::string> problems;
        assert(c.load(problems, error));
        auto r = std::make_shared<JPPart>();
        r->id = "R0603-10k";
        c.addPart(r);
        auto cap = std::make_shared<JPPart>();
        cap->id = "C0603-100n";
        c.addPart(cap);
        assert(c.save(error));
        partUuid = r->uuid;
        capUuid = cap->uuid;
        assert(!partUuid.empty());

        // A reel received, then used, lost, counted and adjusted: on hand is the ledger's sum.
        JPStockStore& s = c.stock();
        JPStockLot lot;
        lot.partUuid = partUuid;
        lot.label = "reel A";
        lot.packaging = "Reel";
        lot.location = "shelf 3";
        JPLedgerEntry received;
        received.kind = JPLedgerEntry::Kind::Received;
        received.quantity = 1000;
        received.cost = 4.5;
        received.reference = "PO-17";
        assert(s.addLot(lot, received, error) && !lot.uuid.empty() && lot.onHand == 1000);
        reelA = lot.uuid;
        for (const auto& [kind, n] : { std::pair { JPLedgerEntry::Kind::Used, 40LL }, std::pair { JPLedgerEntry::Kind::Lost, 2LL },
                                       std::pair { JPLedgerEntry::Kind::Counted, 950LL },
                                       std::pair { JPLedgerEntry::Kind::Adjusted, -5LL } }) {
            JPLedgerEntry e;
            e.lotUuid = reelA;
            e.kind = kind;
            e.quantity = n;
            assert(s.addEntry(e, error) && e.id > 0 && !e.when.empty());
        }
        assert(s.lot(reelA).onHand == 945);
        const auto ledger = s.ledger(reelA);
        assert(ledger.size() == 5 && ledger[0].kind == JPLedgerEntry::Kind::Received && ledger[0].cost == 4.5
               && ledger[0].reference == "PO-17" && ledger[3].kind == JPLedgerEntry::Kind::Counted);

        // A second lot, a strip of cut tape found on the bench (counted in), and one thrown away (closed).
        JPStockLot strip;
        strip.partUuid = partUuid;
        strip.label = "strip";
        JPLedgerEntry counted;
        counted.kind = JPLedgerEntry::Kind::Counted;
        counted.quantity = 30;
        assert(s.addLot(strip, counted, error) && strip.onHand == 30);
        JPStockLot old;
        old.partUuid = partUuid;
        old.label = "old";
        JPLedgerEntry got;
        got.quantity = 100;
        assert(s.addLot(old, got, error));
        old.closed = true;
        old.location = "bin";
        assert(s.updateLot(old, error));
        assert(s.lots(partUuid, false).size() == 2 && s.lots(partUuid, true).size() == 3);
        assert(s.lot(old.uuid).location == "bin" && s.lot(old.uuid).closed);
        assert(s.onHandByPart().at(partUuid) == 975);

        // Attrition: 2 lost of 42 taken; none set until it is.
        const JPStockStore::Attrition a = s.attrition(partUuid);
        assert(a.used == 40 && a.lost == 2 && std::abs(a.rate() - 2.0 / 42) < 1e-9);
        assert(!s.attritionSet(partUuid));
        assert(s.setAttrition(capUuid, 0.1, error) && *s.attritionSet(capUuid) == 0.1);
        assert(s.setAttrition(capUuid, 0.05, error) && *s.attritionSet(capUuid) == 0.05);
    }
    // Kept in the library's file as made, without a save; the library's own save leaves it alone.
    {
        JPConfiguration c(dir.string());
        std::vector<std::string> problems;
        assert(c.load(problems, error) && c.save(error));
        assert(c.stock().lot(reelA).onHand == 945 && c.stock().ledger(reelA).size() == 5);
        assert(*c.stock().attritionSet(capUuid) == 0.05);

        // A job: 40 of the 10k (one placed, one not enabled, one on the other side), 30 of the 100n, 2 of a part
        // the library does not have.
        auto board = std::make_shared<JPBoard>();
        auto add = [&board](const std::string& id, const std::string& part) {
            JPPlacement p;
            p.id = id;
            p.partId = part;
            board->placements.push_back(p);
            return &board->placements.back();
        };
        for (int i = 1; i <= 43; ++i) add("R" + std::to_string(i), "R0603-10k");
        for (int i = 1; i <= 30; ++i) add("C" + std::to_string(i), "C0603-100n");
        add("U1", "LM358");
        add("U2", "LM358");
        board->find("R42")->enabled = false;
        board->find("R43")->side = JPSide::Bottom;
        JPJob job;
        auto l = std::make_unique<JPBoardLocation>();
        l->id = "Brd1";
        l->holder = board;
        JPPlacementsHolderLocation* where = job.addBoardOrPanelLocation(std::move(l));
        job.storePlacedStatus(*where, "R41", true);
        const auto lines = JPShortages::of(c, job);
        assert(lines.size() == 3);
        // The 100n: 30 and 5 % (set) = 2 more; none in stock: short 32, listed first.
        assert(lines[0].partId == "C0603-100n" && lines[0].needed == 30 && lines[0].attrition == 2
               && lines[0].rateFrom == JPShortages::Line::Rate::Set && lines[0].inStock == 0 && lines[0].shortBy == 32);
        // LM358: not the library's, no stock kept.
        assert(lines[1].partId == "LM358" && lines[1].partUuid.empty() && lines[1].needed == 2 && lines[1].shortBy == 0);
        // The 10k: 40 and 2 lost in 42 measured (2 more); 975 in two lots: enough.
        assert(lines[2].partId == "R0603-10k" && lines[2].needed == 40 && lines[2].attrition == 2
               && lines[2].rateFrom == JPShortages::Line::Rate::Measured && lines[2].inStock == 975 && lines[2].lots.size() == 2
               && lines[2].shortBy == 0);
    }
    fs::remove_all(dir);
    return 0;
}
