// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// Runs and the stock they use (DESIGN.md, Storage: runs.db; Stock ledger): a lot loaded on a feeder; a run's feeds
// and placements recorded as they happen; the run ended and its parts written to the lot's ledger, placed as Used
// and fed-not-placed as Lost, once; a run left open (jplacer closed while it ran) written as interrupted when the
// configuration is next read; a stock kept before lots were loaded on feeders brought up to date.
// Tests check with assert(); a Release build must not compile it away.
#undef NDEBUG
#include <cassert>

#include "model/JPConfiguration.h"
#include "model/JPRunLedger.h"

#include <filesystem>

using namespace jf;
namespace fs = std::filesystem;

int main() {
    const fs::path dir = fs::temp_directory_path() / "jplacer-test-runs";
    fs::remove_all(dir);
    fs::create_directories(dir);
    std::string error;
    // A stock as it was kept before lots were loaded on feeders: no feeder column.
    {
        JDatabase db;
        assert(db.open((dir / JPLibraryStore::kFile).string()));
        assert(db.exec("CREATE TABLE stock_lots(uuid TEXT PRIMARY KEY, part_uuid TEXT NOT NULL, label TEXT, packaging TEXT, "
                       "supplier TEXT, sku TEXT, date_code TEXT, lot_code TEXT, location TEXT, note TEXT, created TEXT, "
                       "closed INTEGER NOT NULL DEFAULT 0, on_hand INTEGER NOT NULL DEFAULT 0)"));
        assert(db.exec("INSERT INTO stock_lots(uuid, part_uuid, label, on_hand) VALUES('old', 'p0', 'old reel', 7)"));
    }
    std::string reel, strip, openRun;
    {
        JPConfiguration c(dir.string());
        std::vector<std::string> problems;
        assert(c.load(problems, error));
        JPStockStore& s = c.stock();
        assert(s.lot("old").label == "old reel" && s.lot("old").feederId.empty());

        // Two lots of a part; the reel loaded on feeder F1, then the strip in its place, then the reel again.
        JPStockLot a, b;
        a.partUuid = b.partUuid = "p1";
        a.label = "reel";
        b.label = "strip";
        JPLedgerEntry in;
        in.quantity = 100;
        assert(s.addLot(a, in, error) && s.addLot(b, in, error));
        reel = a.uuid;
        strip = b.uuid;
        assert(s.lotOnFeeder("F1").uuid.empty());
        assert(s.loadLot(reel, "F1", error) && s.lotOnFeeder("F1").uuid == reel);
        assert(s.loadLot(strip, "F1", error) && s.lotOnFeeder("F1").uuid == strip && s.lot(reel).feederId.empty());
        assert(s.loadLot(reel, "F1", error) && s.lotOnFeeder("F1").uuid == reel);

        // A run: 5 fed from the reel (on F1), 4 placed; 2 fed and placed from F2, which carries no lot.
        JPRunStore& r = c.runs();
        JPRunStore::Run run;
        run.job = "/jobs/ctrl.job.xml";
        run.boards = { { "Brd1", "/boards/ctrl.jpboard", "rev B" } };
        assert(r.begin(run) && !run.uuid.empty());
        for (int i = 0; i < 5; ++i) assert(r.fed(run.uuid, "F1", reel, "R0603-10k", "Brd1", "R" + std::to_string(i)));
        for (int i = 0; i < 4; ++i) assert(r.placed(run.uuid, "F1", reel, "R0603-10k", "Brd1", "R" + std::to_string(i)));
        for (int i = 0; i < 2; ++i) {
            assert(r.fed(run.uuid, "F2", "", "C0603-100n", "Brd1", "C" + std::to_string(i)));
            assert(r.placed(run.uuid, "F2", "", "C0603-100n", "Brd1", "C" + std::to_string(i)));
        }
        assert(r.fedByOpenRuns().at(reel) == 5 && r.placedCount(run.uuid) == 6);
        const auto use = r.use(run.uuid);
        assert(use.at(reel).fed == 5 && use.at(reel).placed == 4 && use.at("").placed == 2);

        // Ended and written: 4 used and 1 lost from the reel; its taking no longer counted as open.
        assert(r.end(run.uuid, JPRunStore::Outcome::Finished));
        assert(JPRunLedger::write(r, s, run.uuid, error));
        assert(s.lot(reel).onHand == 95);
        const auto ledger = s.ledger(reel);
        assert(ledger.size() == 3 && ledger[1].kind == JPLedgerEntry::Kind::Used && ledger[1].quantity == 4
               && ledger[2].kind == JPLedgerEntry::Kind::Lost && ledger[2].quantity == 1
               && ledger[1].reference.find("ctrl.job.xml") != std::string::npos);
        assert(r.fedByOpenRuns().count(reel) == 0);
        // Written once.
        assert(JPRunLedger::write(r, s, run.uuid, error) && s.ledger(reel).size() == 3);
        const JPRunStore::Run back = r.run(run.uuid);
        assert(back.outcome == JPRunStore::Outcome::Finished && back.ledgered && back.boards.size() == 1
               && back.boards[0].revision == "rev B" && !back.ended.empty());

        // A second run left open: 3 placed from the reel, then jplacer closed.
        JPRunStore::Run cut;
        cut.job = "/jobs/ctrl.job.xml";
        assert(r.begin(cut));
        openRun = cut.uuid;
        for (int i = 0; i < 3; ++i) {
            assert(r.fed(cut.uuid, "F1", reel, "R0603-10k", "Brd1", "R1" + std::to_string(i)));
            assert(r.placed(cut.uuid, "F1", reel, "R0603-10k", "Brd1", "R1" + std::to_string(i)));
        }
        assert(r.runs(10).size() == 2 && r.runs(10).front().uuid == cut.uuid);

        // A closed lot comes off its feeder.
        JPStockLot closing = s.lot(strip);
        assert(s.loadLot(strip, "F3", error));
        closing.closed = true;
        assert(s.updateLot(closing, error) && s.lot(strip).feederId.empty() && s.lotOnFeeder("F3").uuid.empty());
    }
    // Read again: the open run ended as interrupted and its parts written.
    {
        JPConfiguration c(dir.string());
        std::vector<std::string> problems;
        assert(c.load(problems, error) && problems.empty());
        const JPRunStore::Run cut = c.runs().run(openRun);
        assert(cut.outcome == JPRunStore::Outcome::Interrupted && cut.ledgered);
        assert(c.stock().lot(reel).onHand == 92 && c.runs().unledgered().empty());
    }
    fs::remove_all(dir);
    return 0;
}
