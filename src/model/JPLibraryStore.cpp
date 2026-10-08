// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPLibraryStore.h"

#include "openpnp/JPXmlJson.h"

#include <functional>
#include <map>

inline namespace jf {

namespace {

using B = JDatabase::JBind;

const char* kSchemaSql = R"(
CREATE TABLE IF NOT EXISTS meta(key TEXT PRIMARY KEY, value TEXT);
CREATE TABLE IF NOT EXISTS packages(uuid TEXT PRIMARY KEY, id TEXT NOT NULL UNIQUE COLLATE NOCASE, data TEXT NOT NULL);
CREATE TABLE IF NOT EXISTS package_akas(package_uuid TEXT NOT NULL, text TEXT NOT NULL);
CREATE TABLE IF NOT EXISTS parts(uuid TEXT PRIMARY KEY, id TEXT NOT NULL UNIQUE COLLATE NOCASE, name TEXT, value TEXT,
                                 datasheet TEXT, package_id TEXT, data TEXT NOT NULL);
CREATE TABLE IF NOT EXISTS identifiers(part_uuid TEXT NOT NULL, kind TEXT NOT NULL, org TEXT, code TEXT NOT NULL);
CREATE TABLE IF NOT EXISTS akas(part_uuid TEXT NOT NULL, field TEXT NOT NULL, text TEXT NOT NULL, learned_from TEXT,
                                learned_when TEXT);
CREATE TABLE IF NOT EXISTS packagings(part_uuid TEXT NOT NULL, kind TEXT NOT NULL, tape_width REAL, pitch REAL,
                                      tape_type TEXT, rotation REAL, quantity INTEGER, note TEXT);
CREATE TABLE IF NOT EXISTS offers(part_uuid TEXT NOT NULL, supplier TEXT, sku TEXT, packaging TEXT, moq INTEGER,
                                  price_breaks TEXT, link TEXT, last_price TEXT, last_when TEXT);
CREATE TABLE IF NOT EXISTS manufacturers(name TEXT NOT NULL, aka TEXT);
CREATE TABLE IF NOT EXISTS footprints(uuid TEXT PRIMARY KEY, name TEXT NOT NULL, package_id TEXT, data TEXT NOT NULL,
                                      zero_rotation REAL, source TEXT);
CREATE TABLE IF NOT EXISTS footprint_names(footprint_uuid TEXT NOT NULL, name TEXT NOT NULL);
CREATE INDEX IF NOT EXISTS footprint_names_name ON footprint_names(name COLLATE NOCASE);
CREATE INDEX IF NOT EXISTS identifiers_code ON identifiers(code COLLATE NOCASE);
CREATE INDEX IF NOT EXISTS akas_text ON akas(text COLLATE NOCASE);
CREATE INDEX IF NOT EXISTS package_akas_text ON package_akas(text COLLATE NOCASE);
)";

} // namespace

bool JPLibraryStore::open(const std::string& path, std::string& error) {
    if (!m_db.open(path) || !m_db.exec(kSchemaSql)) {
        error = "Cannot open the library " + path + ": " + m_db.lastError();
        return false;
    }
    const std::string schema = meta("schema");
    if (!schema.empty() && std::stoi(schema) > kSchema) {
        error = "The library " + path + " is of a newer jplacer (schema " + schema + "); this one reads " +
                std::to_string(kSchema);
        return false;
    }
    // An older one is brought up to this schema: its new tables made above, empty.
    if (schema.empty() || std::stoi(schema) < kSchema) setMeta("schema", std::to_string(kSchema));
    return true;
}

std::string JPLibraryStore::meta(const std::string& key) {
    std::string value;
    m_db.query("SELECT value FROM meta WHERE key = ?", { B::from(key) }, [&value](const JDatabase::JRow& r) { value = r.text(0); });
    return value;
}

bool JPLibraryStore::setMeta(const std::string& key, const std::string& value) {
    return m_db.exec("INSERT OR REPLACE INTO meta(key, value) VALUES(?, ?)", { B::from(key), B::from(value) });
}

bool JPLibraryStore::load(Contents& out, std::string& error) {
    out = Contents();
    auto& parts = out.parts;
    auto& packages = out.packages;
    auto& manufacturers = out.manufacturers;
    std::map<std::string, JPLibraryFootprint*> footprintByUuid;
    std::map<std::string, JPPart*> partByUuid;
    auto query = [this](const char* sql, std::function<void(const JDatabase::JRow&)> row) { return m_db.query(sql, {}, row) >= 0; };
    const bool ok =
        query("SELECT uuid, data FROM packages ORDER BY rowid", [&](const JDatabase::JRow& r) {
            const std::optional<JJson> j = JJson::tryParse(r.text(1));
            if (!j) return;
            auto k = std::make_shared<JPPackage>(JPPackage::fromXml(JPXmlJson::element(*j)));
            k->uuid = r.text(0);
            packages.push_back(k);
        })
        && query("SELECT package_uuid, text FROM package_akas ORDER BY rowid", [&](const JDatabase::JRow& r) {
            out.packageNames.emplace_back(r.text(0), r.text(1));
        })
        && query("SELECT uuid, name, package_id, data, zero_rotation, source FROM footprints ORDER BY rowid",
                 [&](const JDatabase::JRow& r) {
                     const std::optional<JJson> j = JJson::tryParse(r.text(3));
                     auto f = std::make_shared<JPLibraryFootprint>();
                     f->uuid = r.text(0);
                     f->name = r.text(1);
                     f->packageId = r.text(2);
                     if (j) f->geometry = JPFootprint::fromXml(JPXmlJson::element(*j));
                     f->zeroRotationDeg = r.real(4);
                     f->source = r.text(5);
                     footprintByUuid[f->uuid] = f.get();
                     out.footprints.push_back(f);
                 })
        && query("SELECT footprint_uuid, name FROM footprint_names ORDER BY rowid", [&](const JDatabase::JRow& r) {
            if (auto it = footprintByUuid.find(r.text(0)); it != footprintByUuid.end()) it->second->cadNames.push_back(r.text(1));
        })
        && query("SELECT uuid, value, datasheet, data FROM parts ORDER BY rowid", [&](const JDatabase::JRow& r) {
            const std::optional<JJson> j = JJson::tryParse(r.text(3));
            if (!j) return;
            auto p = std::make_shared<JPPart>(JPPart::fromXml(JPXmlJson::element(*j)));
            p->uuid = r.text(0);
            p->value = r.text(1);
            p->datasheet = r.text(2);
            partByUuid[p->uuid] = p.get();
            parts.push_back(p);
        })
        && query("SELECT part_uuid, kind, org, code FROM identifiers ORDER BY rowid", [&](const JDatabase::JRow& r) {
            if (auto it = partByUuid.find(r.text(0)); it != partByUuid.end())
                it->second->identifiers.push_back({ r.text(1), r.text(2), r.text(3) });
        })
        && query("SELECT part_uuid, field, text, learned_from, learned_when FROM akas ORDER BY rowid", [&](const JDatabase::JRow& r) {
            if (auto it = partByUuid.find(r.text(0)); it != partByUuid.end())
                it->second->akas.push_back({ r.text(1), r.text(2), r.text(3), r.text(4) });
        })
        && query("SELECT part_uuid, kind, tape_width, pitch, tape_type, rotation, quantity, note FROM packagings ORDER BY rowid",
                 [&](const JDatabase::JRow& r) {
                     if (auto it = partByUuid.find(r.text(0)); it != partByUuid.end())
                         it->second->packagings.push_back({ r.text(1), r.real(2), r.real(3), r.text(4), r.real(5),
                                                            int(r.integer(6)), r.text(7) });
                 })
        && query("SELECT part_uuid, supplier, sku, packaging, moq, price_breaks, link, last_price, last_when FROM offers ORDER BY rowid",
                 [&](const JDatabase::JRow& r) {
                     if (auto it = partByUuid.find(r.text(0)); it != partByUuid.end())
                         it->second->offers.push_back({ r.text(1), r.text(2), r.text(3), int(r.integer(4)), r.text(5),
                                                        r.text(6), r.text(7), r.text(8) });
                 })
        && query("SELECT name, aka FROM manufacturers ORDER BY rowid", [&](const JDatabase::JRow& r) {
            const std::string name = r.text(0), aka = r.text(1);
            JPManufacturer* m = nullptr;
            for (JPManufacturer& x : manufacturers)
                if (x.name == name) m = &x;
            if (!m) {
                manufacturers.push_back({ name, {} });
                m = &manufacturers.back();
            }
            if (!aka.empty()) m->akas.push_back(aka);
        });
    if (!ok) {
        error = "Reading the library: " + m_db.lastError();
        return false;
    }
    return true;
}

bool JPLibraryStore::save(const Contents& in, std::string& error) {
    const auto& parts = in.parts;
    const auto& packages = in.packages;
    const auto& manufacturers = in.manufacturers;
    bool ok = m_db.exec("BEGIN IMMEDIATE");
    for (const char* table : { "packages", "package_akas", "parts", "identifiers", "akas", "packagings", "offers", "manufacturers",
                               "footprints", "footprint_names" })
        ok = ok && m_db.exec(std::string("DELETE FROM ") + table);
    for (const auto& f : in.footprints) {
        if (!ok) break;
        ok = m_db.exec("INSERT INTO footprints(uuid, name, package_id, data, zero_rotation, source) VALUES(?, ?, ?, ?, ?, ?)",
                       { B::from(f->uuid), B::from(f->name), B::from(f->packageId), B::from(JPXmlJson::from(f->geometry.toXml()).dump()),
                         B::from(f->zeroRotationDeg), B::from(f->source) });
        for (const std::string& n : f->cadNames)
            ok = ok && m_db.exec("INSERT INTO footprint_names(footprint_uuid, name) VALUES(?, ?)", { B::from(f->uuid), B::from(n) });
    }
    for (const JPManufacturer& m : manufacturers) {
        if (!ok) break;
        // One row with no AKA keeps a name that has none.
        ok = m_db.exec("INSERT INTO manufacturers(name, aka) VALUES(?, ?)", { B::from(m.name), B::from("") });
        for (const std::string& a : m.akas)
            ok = ok && m_db.exec("INSERT INTO manufacturers(name, aka) VALUES(?, ?)", { B::from(m.name), B::from(a) });
    }
    for (const auto& k : packages) {
        if (!ok) break;
        ok = m_db.exec("INSERT INTO packages(uuid, id, data) VALUES(?, ?, ?)",
                       { B::from(k->uuid), B::from(k->id), B::from(JPXmlJson::from(k->toXml()).dump()) });
    }
    for (const auto& p : parts) {
        if (!ok) break;
        ok = m_db.exec("INSERT INTO parts(uuid, id, name, value, datasheet, package_id, data) VALUES(?, ?, ?, ?, ?, ?, ?)",
                       { B::from(p->uuid), B::from(p->id), B::from(p->name.value_or("")), B::from(p->value),
                         B::from(p->datasheet), B::from(p->packageId),
                         B::from(JPXmlJson::from(p->toXml()).dump()) });
        for (const auto& i : p->identifiers)
            ok = ok && m_db.exec("INSERT INTO identifiers(part_uuid, kind, org, code) VALUES(?, ?, ?, ?)",
                                 { B::from(p->uuid), B::from(i.kind), B::from(i.org), B::from(i.code) });
        for (const auto& a : p->akas)
            ok = ok && m_db.exec("INSERT INTO akas(part_uuid, field, text, learned_from, learned_when) VALUES(?, ?, ?, ?, ?)",
                                 { B::from(p->uuid), B::from(a.field), B::from(a.text), B::from(a.learnedFrom), B::from(a.when) });
        for (const auto& k : p->packagings)
            ok = ok && m_db.exec("INSERT INTO packagings(part_uuid, kind, tape_width, pitch, tape_type, rotation, quantity, note) "
                                 "VALUES(?, ?, ?, ?, ?, ?, ?, ?)",
                                 { B::from(p->uuid), B::from(k.kind), B::from(k.tapeWidthMm), B::from(k.pitchMm), B::from(k.tapeType),
                                   B::from(k.rotationDeg), B::from(k.quantity), B::from(k.note) });
        for (const auto& o : p->offers)
            ok = ok && m_db.exec("INSERT INTO offers(part_uuid, supplier, sku, packaging, moq, price_breaks, link, last_price, last_when) "
                                 "VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?)",
                                 { B::from(p->uuid), B::from(o.supplier), B::from(o.sku), B::from(o.packaging), B::from(o.moq),
                                   B::from(o.priceBreaks), B::from(o.link), B::from(o.lastPrice), B::from(o.lastWhen) });
    }
    if (!ok) {
        error = "Writing the library: " + m_db.lastError();
        m_db.exec("ROLLBACK");
        return false;
    }
    if (!m_db.exec("COMMIT")) {
        error = "Writing the library: " + m_db.lastError();
        m_db.exec("ROLLBACK");
        return false;
    }
    return true;
}

} // inline namespace jf
