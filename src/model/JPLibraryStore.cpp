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
    if (schema.empty()) setMeta("schema", std::to_string(kSchema));
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

bool JPLibraryStore::load(std::vector<std::shared_ptr<JPPart>>& parts, std::vector<std::shared_ptr<JPPackage>>& packages,
                          std::string& error) {
    parts.clear();
    packages.clear();
    std::map<std::string, JPPackage*> packageByUuid;
    std::map<std::string, JPPart*> partByUuid;
    auto query = [this](const char* sql, std::function<void(const JDatabase::JRow&)> row) { return m_db.query(sql, {}, row) >= 0; };
    const bool ok =
        query("SELECT uuid, data FROM packages ORDER BY rowid", [&](const JDatabase::JRow& r) {
            const std::optional<JJson> j = JJson::tryParse(r.text(1));
            if (!j) return;
            auto k = std::make_shared<JPPackage>(JPPackage::fromXml(JPXmlJson::element(*j)));
            k->uuid = r.text(0);
            packageByUuid[k->uuid] = k.get();
            packages.push_back(k);
        })
        && query("SELECT package_uuid, text FROM package_akas ORDER BY rowid", [&](const JDatabase::JRow& r) {
            if (auto it = packageByUuid.find(r.text(0)); it != packageByUuid.end()) it->second->akas.push_back(r.text(1));
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
        });
    if (!ok) {
        error = "Reading the library: " + m_db.lastError();
        return false;
    }
    return true;
}

bool JPLibraryStore::save(const std::vector<std::shared_ptr<JPPart>>& parts,
                          const std::vector<std::shared_ptr<JPPackage>>& packages, std::string& error) {
    bool ok = m_db.exec("BEGIN IMMEDIATE") && m_db.exec("DELETE FROM packages") && m_db.exec("DELETE FROM package_akas")
              && m_db.exec("DELETE FROM parts") && m_db.exec("DELETE FROM identifiers") && m_db.exec("DELETE FROM akas");
    for (const auto& k : packages) {
        if (!ok) break;
        ok = m_db.exec("INSERT INTO packages(uuid, id, data) VALUES(?, ?, ?)",
                       { B::from(k->uuid), B::from(k->id), B::from(JPXmlJson::from(k->toXml()).dump()) });
        for (const std::string& a : k->akas)
            ok = ok && m_db.exec("INSERT INTO package_akas(package_uuid, text) VALUES(?, ?)", { B::from(k->uuid), B::from(a) });
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
