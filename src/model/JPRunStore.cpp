// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPRunStore.h"

#include "common/JPUuid.h"
#include "common/JPWhen.h"

inline namespace jf {

namespace {

using B = JDatabase::JBind;

const char* kSchemaSql = R"(
CREATE TABLE IF NOT EXISTS runs(uuid TEXT PRIMARY KEY, job TEXT, started TEXT NOT NULL, ended TEXT, outcome TEXT NOT NULL,
  ledgered INTEGER NOT NULL DEFAULT 0);
CREATE TABLE IF NOT EXISTS run_boards(run_uuid TEXT NOT NULL, board_id TEXT, file TEXT, revision TEXT);
CREATE TABLE IF NOT EXISTS run_events(id INTEGER PRIMARY KEY AUTOINCREMENT, run_uuid TEXT NOT NULL, at TEXT NOT NULL,
  kind TEXT NOT NULL, feeder_id TEXT, lot_uuid TEXT, part_id TEXT, board_id TEXT, placement_id TEXT);
CREATE INDEX IF NOT EXISTS run_events_run ON run_events(run_uuid);
CREATE INDEX IF NOT EXISTS run_boards_run ON run_boards(run_uuid);
)";

} // namespace

const char* JPRunStore::outcomeName(Outcome o) {
    switch (o) {
        case Outcome::Open:        return "Open";
        case Outcome::Finished:    return "Finished";
        case Outcome::Stopped:     return "Stopped";
        case Outcome::Interrupted: return "Interrupted";
    }
    return "Open";
}

JPRunStore::Outcome JPRunStore::outcomeFrom(const std::string& s) {
    for (Outcome o : { Outcome::Open, Outcome::Finished, Outcome::Stopped, Outcome::Interrupted })
        if (s == outcomeName(o)) return o;
    return Outcome::Open;
}

bool JPRunStore::open(const std::string& path, std::string& error) {
    if (!m_db.open(path) || !m_db.exec(kSchemaSql)) {
        error = "Could not open the runs in " + path;
        return false;
    }
    return true;
}

bool JPRunStore::begin(Run& run) {
    if (!isOpen()) return false;
    if (run.uuid.empty()) run.uuid = JPUuid::make();
    if (run.started.empty()) run.started = JPWhen::now();
    run.outcome = Outcome::Open;
    return m_db.transaction([&] {
        if (!m_db.exec("INSERT INTO runs(uuid, job, started, outcome) VALUES(?, ?, ?, ?)",
                       { B::from(run.uuid), B::from(run.job), B::from(run.started), B::from(outcomeName(run.outcome)) }))
            return false;
        for (const Board& b : run.boards)
            if (!m_db.exec("INSERT INTO run_boards(run_uuid, board_id, file, revision) VALUES(?, ?, ?, ?)",
                           { B::from(run.uuid), B::from(b.id), B::from(b.file), B::from(b.revision) }))
                return false;
        return true;
    });
}

bool JPRunStore::event(const char* kind, const std::string& runUuid, const std::string& feederId, const std::string& lotUuid,
                       const std::string& partId, const std::string& boardId, const std::string& placementId) {
    return isOpen() && m_db.exec("INSERT INTO run_events(run_uuid, at, kind, feeder_id, lot_uuid, part_id, board_id, placement_id) "
                                 "VALUES(?, ?, ?, ?, ?, ?, ?, ?)",
                                 { B::from(runUuid), B::from(JPWhen::now()), B::from(kind), B::from(feederId), B::from(lotUuid),
                                   B::from(partId), B::from(boardId), B::from(placementId) });
}

bool JPRunStore::fed(const std::string& runUuid, const std::string& feederId, const std::string& lotUuid,
                     const std::string& partId, const std::string& boardId, const std::string& placementId) {
    return event("Fed", runUuid, feederId, lotUuid, partId, boardId, placementId);
}

bool JPRunStore::placed(const std::string& runUuid, const std::string& feederId, const std::string& lotUuid,
                        const std::string& partId, const std::string& boardId, const std::string& placementId) {
    return event("Placed", runUuid, feederId, lotUuid, partId, boardId, placementId);
}

bool JPRunStore::end(const std::string& runUuid, Outcome outcome) {
    return isOpen() && m_db.exec("UPDATE runs SET ended = ?, outcome = ? WHERE uuid = ?",
                                 { B::from(JPWhen::now()), B::from(outcomeName(outcome)), B::from(runUuid) });
}

bool JPRunStore::setLedgered(const std::string& runUuid) {
    return isOpen() && m_db.exec("UPDATE runs SET ledgered = 1 WHERE uuid = ?", { B::from(runUuid) });
}

std::vector<JPRunStore::Run> JPRunStore::read(const std::string& where, const std::vector<JDatabase::JBind>& binds) const {
    std::vector<Run> out;
    if (!isOpen()) return out;
    m_db.query("SELECT uuid, job, started, ended, outcome, ledgered FROM runs " + where, binds, [&out](const JDatabase::JRow& r) {
        Run run;
        run.uuid = r.text(0);
        run.job = r.text(1);
        run.started = r.text(2);
        run.ended = r.isNull(3) ? std::string() : r.text(3);
        run.outcome = outcomeFrom(r.text(4));
        run.ledgered = r.integer(5) != 0;
        out.push_back(run);
    });
    for (Run& run : out)
        m_db.query("SELECT board_id, file, revision FROM run_boards WHERE run_uuid = ? ORDER BY rowid", { B::from(run.uuid) },
                   [&run](const JDatabase::JRow& r) { run.boards.push_back({ r.text(0), r.text(1), r.text(2) }); });
    return out;
}

std::vector<JPRunStore::Run> JPRunStore::runs(size_t limit) const {
    return read("ORDER BY started DESC, rowid DESC LIMIT ?", { B::from(int64_t(limit)) });
}

std::vector<JPRunStore::Run> JPRunStore::unledgered() const {
    return read("WHERE ledgered = 0 ORDER BY started, rowid", {});
}

JPRunStore::Run JPRunStore::run(const std::string& uuid) const {
    const auto found = read("WHERE uuid = ?", { B::from(uuid) });
    return found.empty() ? Run() : found.front();
}

std::map<std::string, JPRunStore::Use> JPRunStore::use(const std::string& runUuid) const {
    std::map<std::string, Use> out;
    if (!isOpen()) return out;
    m_db.query("SELECT lot_uuid, kind, COUNT(*) FROM run_events WHERE run_uuid = ? GROUP BY lot_uuid, kind", { B::from(runUuid) },
               [&out](const JDatabase::JRow& r) {
                   Use& u = out[r.text(0)];
                   (r.text(1) == "Placed" ? u.placed : u.fed) = r.integer(2);
               });
    return out;
}

std::map<std::string, long long> JPRunStore::fedByOpenRuns() const {
    std::map<std::string, long long> out;
    if (!isOpen()) return out;
    m_db.query("SELECT run_events.lot_uuid, COUNT(*) FROM run_events JOIN runs ON runs.uuid = run_events.run_uuid "
               "WHERE runs.ledgered = 0 AND run_events.kind = 'Fed' AND run_events.lot_uuid <> '' GROUP BY run_events.lot_uuid",
               {}, [&out](const JDatabase::JRow& r) { out[r.text(0)] = r.integer(1); });
    return out;
}

long long JPRunStore::placedCount(const std::string& runUuid) const {
    long long n = 0;
    if (isOpen())
        m_db.query("SELECT COUNT(*) FROM run_events WHERE run_uuid = ? AND kind = 'Placed'", { B::from(runUuid) },
                   [&n](const JDatabase::JRow& r) { n = r.integer(0); });
    return n;
}

} // inline namespace jf
