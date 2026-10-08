// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include <j/db/Database.h>

#include <map>
#include <string>
#include <vector>

inline namespace jf {

// The runs of jobs (DESIGN.md, Storage: runs.db): each run, the boards it built at their revisions, and what it
// did as it did it (a part fed from a feeder, a placement placed), appended as it happens, so a run cut short by
// a crash still says what it used. Written from the job's thread; the database serialises.
class JPRunStore {
public:
    static constexpr const char* kFile = "runs.db";

    enum class Outcome { Open, Finished, Stopped, Interrupted };
    struct Board {
        std::string id;         // its unique id in the job ("Brd1")
        std::string file;
        std::string revision;   // empty: the board keeps no revisions
    };
    struct Run {
        std::string        uuid, job, started, ended;
        Outcome            outcome = Outcome::Open;
        bool               ledgered = false;   // its parts written to the stock's ledger
        std::vector<Board> boards;
    };
    // What a run took from one lot (by its uuid; empty: from a feeder carrying none): parts fed, and placed.
    struct Use {
        long long fed = 0;
        long long placed = 0;
    };

    bool open(const std::string& path, std::string& error);
    bool isOpen() const { return m_db.isOpen(); }

    // A run begun (given its uuid and start); false when it could not be kept.
    bool begin(Run& run);
    // A part fed from `feederId`, from the lot it carries (empty: none), for a placement.
    bool fed(const std::string& runUuid, const std::string& feederId, const std::string& lotUuid,
             const std::string& partId, const std::string& boardId, const std::string& placementId);
    bool placed(const std::string& runUuid, const std::string& feederId, const std::string& lotUuid,
                const std::string& partId, const std::string& boardId, const std::string& placementId);
    bool end(const std::string& runUuid, Outcome outcome);
    bool setLedgered(const std::string& runUuid);

    // The runs, newest first (at most `limit`).
    std::vector<Run> runs(size_t limit) const;
    Run run(const std::string& uuid) const;
    // The runs whose parts are not in the ledger yet, oldest first (open ones among them).
    std::vector<Run> unledgered() const;
    // A run's parts by lot.
    std::map<std::string, Use> use(const std::string& runUuid) const;
    // Parts fed from each lot by runs still open (what a feeder's lot holds less what the run under way took).
    std::map<std::string, long long> fedByOpenRuns() const;
    // How many placements a run placed.
    long long placedCount(const std::string& runUuid) const;

    static const char* outcomeName(Outcome o);
    static Outcome     outcomeFrom(const std::string& s);

private:
    std::vector<Run> read(const std::string& where, const std::vector<JDatabase::JBind>& binds) const;
    bool event(const char* kind, const std::string& runUuid, const std::string& feederId, const std::string& lotUuid,
               const std::string& partId, const std::string& boardId, const std::string& placementId);

    mutable JDatabase m_db;
};

} // inline namespace jf
