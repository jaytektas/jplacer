// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPStockStore.h"

#include "common/JPUuid.h"
#include "common/JPWhen.h"

inline namespace jf {

namespace {

using B = JDatabase::JBind;

const char* kSchemaSql = R"(
CREATE TABLE IF NOT EXISTS stock_lots(uuid TEXT PRIMARY KEY, part_uuid TEXT NOT NULL, label TEXT, packaging TEXT,
  supplier TEXT, sku TEXT, date_code TEXT, lot_code TEXT, location TEXT, note TEXT, created TEXT,
  closed INTEGER NOT NULL DEFAULT 0, on_hand INTEGER NOT NULL DEFAULT 0);
CREATE INDEX IF NOT EXISTS stock_lots_part ON stock_lots(part_uuid);
CREATE TABLE IF NOT EXISTS ledger(id INTEGER PRIMARY KEY AUTOINCREMENT, lot_uuid TEXT NOT NULL, at TEXT NOT NULL,
  kind TEXT NOT NULL, quantity INTEGER NOT NULL, cost REAL, reference TEXT, note TEXT);
CREATE INDEX IF NOT EXISTS ledger_lot ON ledger(lot_uuid);
CREATE TABLE IF NOT EXISTS stock_parts(part_uuid TEXT PRIMARY KEY, attrition REAL);
)";

const char* kLotColumns = "uuid, part_uuid, label, packaging, supplier, sku, date_code, lot_code, location, note, created, "
                          "closed, on_hand";

JPStockLot lotFrom(const JDatabase::JRow& r) {
    JPStockLot l;
    l.uuid = r.text(0);
    l.partUuid = r.text(1);
    l.label = r.text(2);
    l.packaging = r.text(3);
    l.supplier = r.text(4);
    l.sku = r.text(5);
    l.dateCode = r.text(6);
    l.lotCode = r.text(7);
    l.location = r.text(8);
    l.note = r.text(9);
    l.created = r.text(10);
    l.closed = r.integer(11) != 0;
    l.onHand = r.integer(12);
    return l;
}

} // namespace

bool JPStockStore::open(const std::string& path, std::string& error) {
    if (!m_db.open(path) || !m_db.exec(kSchemaSql)) {
        error = "Could not open the stock in " + path;
        return false;
    }
    return true;
}

std::vector<JPStockLot> JPStockStore::lots(const std::string& partUuid, bool closed) const {
    std::vector<JPStockLot> out;
    if (!isOpen()) return out;
    m_db.query(std::string("SELECT ") + kLotColumns + " FROM stock_lots WHERE part_uuid = ?" +
                   (closed ? "" : " AND closed = 0") + " ORDER BY rowid",
               { B::from(partUuid) }, [&out](const JDatabase::JRow& r) { out.push_back(lotFrom(r)); });
    return out;
}

JPStockLot JPStockStore::lot(const std::string& uuid) const {
    JPStockLot out;
    if (!isOpen()) return out;
    m_db.query(std::string("SELECT ") + kLotColumns + " FROM stock_lots WHERE uuid = ?", { B::from(uuid) },
               [&out](const JDatabase::JRow& r) { out = lotFrom(r); });
    return out;
}

std::vector<JPLedgerEntry> JPStockStore::ledger(const std::string& lotUuid) const {
    std::vector<JPLedgerEntry> out;
    if (!isOpen()) return out;
    m_db.query("SELECT id, lot_uuid, at, kind, quantity, cost, reference, note FROM ledger WHERE lot_uuid = ? ORDER BY id",
               { B::from(lotUuid) }, [&out](const JDatabase::JRow& r) {
                   JPLedgerEntry e;
                   e.id = r.integer(0);
                   e.lotUuid = r.text(1);
                   e.when = r.text(2);
                   e.kind = JPLedgerEntry::kindFrom(r.text(3));
                   e.quantity = r.integer(4);
                   e.cost = r.isNull(5) ? 0 : r.real(5);
                   e.reference = r.text(6);
                   e.note = r.text(7);
                   out.push_back(e);
               });
    return out;
}

std::map<std::string, long long> JPStockStore::onHandByPart() const {
    std::map<std::string, long long> out;
    if (!isOpen()) return out;
    m_db.query("SELECT part_uuid, SUM(on_hand) FROM stock_lots WHERE closed = 0 GROUP BY part_uuid", {},
               [&out](const JDatabase::JRow& r) { out[r.text(0)] = r.integer(1); });
    return out;
}

JPStockStore::Attrition JPStockStore::attrition(const std::string& partUuid) const {
    Attrition a;
    if (!isOpen()) return a;
    m_db.query("SELECT ledger.kind, SUM(ledger.quantity) FROM ledger JOIN stock_lots ON stock_lots.uuid = ledger.lot_uuid "
               "WHERE stock_lots.part_uuid = ? AND ledger.kind IN ('Used', 'Lost') GROUP BY ledger.kind",
               { B::from(partUuid) }, [&a](const JDatabase::JRow& r) {
                   (r.text(0) == "Used" ? a.used : a.lost) = r.integer(1);
               });
    return a;
}

std::optional<double> JPStockStore::attritionSet(const std::string& partUuid) const {
    std::optional<double> out;
    if (isOpen())
        m_db.query("SELECT attrition FROM stock_parts WHERE part_uuid = ?", { B::from(partUuid) }, [&out](const JDatabase::JRow& r) {
            if (!r.isNull(0)) out = r.real(0);
        });
    return out;
}

bool JPStockStore::setAttrition(const std::string& partUuid, std::optional<double> rate, std::string& error) {
    const bool ok = isOpen() && (rate ? m_db.exec("INSERT INTO stock_parts(part_uuid, attrition) VALUES(?, ?) "
                                                  "ON CONFLICT(part_uuid) DO UPDATE SET attrition = excluded.attrition",
                                                  { B::from(partUuid), B::from(*rate) })
                                      : m_db.exec("DELETE FROM stock_parts WHERE part_uuid = ?", { B::from(partUuid) }));
    if (!ok) error = "Could not keep the part's attrition";
    return ok;
}

bool JPStockStore::addLot(JPStockLot& lot, JPLedgerEntry first, std::string& error) {
    if (!isOpen()) {
        error = "The stock is not open";
        return false;
    }
    if (lot.uuid.empty()) lot.uuid = JPUuid::make();
    if (lot.created.empty()) lot.created = JPWhen::now();
    first.lotUuid = lot.uuid;
    const bool ok = m_db.transaction([&] {
        return m_db.exec(std::string("INSERT INTO stock_lots(") + kLotColumns + ") VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, 0)",
                         { B::from(lot.uuid), B::from(lot.partUuid), B::from(lot.label), B::from(lot.packaging),
                           B::from(lot.supplier), B::from(lot.sku), B::from(lot.dateCode), B::from(lot.lotCode),
                           B::from(lot.location), B::from(lot.note), B::from(lot.created), B::from(lot.closed ? 1 : 0) })
               && insertEntry(first);
    });
    if (!ok) {
        if (error.empty()) error = "Could not add the lot to the stock";
        return false;
    }
    lot.onHand = first.apply(0);
    return true;
}

bool JPStockStore::updateLot(const JPStockLot& lot, std::string& error) {
    if (isOpen() && m_db.exec("UPDATE stock_lots SET label = ?, packaging = ?, supplier = ?, sku = ?, date_code = ?, lot_code = ?, "
                              "location = ?, note = ?, closed = ? WHERE uuid = ?",
                              { B::from(lot.label), B::from(lot.packaging), B::from(lot.supplier), B::from(lot.sku),
                                B::from(lot.dateCode), B::from(lot.lotCode), B::from(lot.location), B::from(lot.note),
                                B::from(lot.closed ? 1 : 0), B::from(lot.uuid) }))
        return true;
    error = "Could not change the lot " + lot.label;
    return false;
}

bool JPStockStore::addEntry(JPLedgerEntry& e, std::string& error) {
    const bool ok = isOpen() && m_db.transaction([&] { return insertEntry(e); });
    if (!ok) error = "Could not add to the lot's ledger";
    return ok;
}

bool JPStockStore::insertEntry(JPLedgerEntry& e) {
    if (e.when.empty()) e.when = JPWhen::now();
    if (!m_db.exec("INSERT INTO ledger(lot_uuid, at, kind, quantity, cost, reference, note) VALUES(?, ?, ?, ?, ?, ?, ?)",
                   { B::from(e.lotUuid), B::from(e.when), B::from(JPLedgerEntry::kindName(e.kind)), B::from(int64_t(e.quantity)),
                     e.cost > 0 ? B::from(e.cost) : B::null(), B::from(e.reference), B::from(e.note) }))
        return false;
    e.id = m_db.lastInsertRowId();
    return recount(e.lotUuid);
}

bool JPStockStore::recount(const std::string& lotUuid) {
    long long onHand = 0;
    for (const JPLedgerEntry& e : ledger(lotUuid)) onHand = e.apply(onHand);
    return m_db.exec("UPDATE stock_lots SET on_hand = ? WHERE uuid = ?", { B::from(int64_t(onHand)), B::from(lotUuid) });
}

} // inline namespace jf
