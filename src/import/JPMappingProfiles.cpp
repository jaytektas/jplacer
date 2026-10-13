// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPMappingProfiles.h"

#include "common/JPWholeFile.h"

#include "model/JPLengthUnits.h"

#include <algorithm>
#include <filesystem>

inline namespace jf {

namespace {

JPImportSource::Role roleFrom(const std::string& s) {
    if (s == "bom") return JPImportSource::Role::Bom;
    if (s == "cpl") return JPImportSource::Role::Cpl;
    return JPImportSource::Role::Other;
}

JPMappingProfiles::Profile profileFrom(const JJson& p) {
    JPMappingProfiles::Profile out;
    if (p["name"].isString()) out.name = p["name"].str();
    out.role = roleFrom(p["role"].isString() ? p["role"].str() : std::string());
    if (p["units"].isString()) JPLengthUnits::fromName(p["units"].str(), out.units);
    if (p["columns"].isObject())
        for (const auto& [header, field] : p["columns"].obj())
            if (field.isString()) out.columns[header] = field.str();
    return out;
}

JJson json(const JPMappingProfiles::Profile& p) {
    JJson o = JJson::object();
    o["name"] = p.name;
    o["role"] = JPImportSource::roleName(p.role);
    o["units"] = JPLengthUnits::name(p.units);
    JJson cols = JJson::object();
    for (const auto& [header, field] : p.columns) cols[header] = field;
    o["columns"] = cols;
    return o;
}

// A profile's columns are the header's, every one.
bool sameHeader(const JPMappingProfiles::Profile& p, const std::vector<std::string>& header) {
    size_t named = 0;
    for (const std::string& h : header)
        if (!h.empty()) {
            if (!p.columns.count(h)) return false;
            ++named;
        }
    return named == p.columns.size();
}

} // namespace

bool JPMappingProfiles::load(const std::string& path, std::string& error) {
    profiles.clear();
    last.clear();
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) return true;
    const std::optional<JJson> j = JJson::tryParseFile(path);
    if (!j || !(*j)["profiles"].isArray()) {
        error = "Not a file of import profiles: " + path;
        return false;
    }
    for (const JJson& p : (*j)["profiles"].arr()) {
        Profile out = profileFrom(p);
        if (!out.name.empty()) profiles.push_back(out);
    }
    if ((*j)["last"].isArray())
        for (const JJson& p : (*j)["last"].arr()) {
            Profile out = profileFrom(p);
            out.name = kLast;
            if (!out.columns.empty()) last.push_back(out);
        }
    return true;
}

bool JPMappingProfiles::save(const std::string& path, std::string& error) const {
    JJson j = JJson::object();
    JJson list = JJson::array();
    for (const Profile& p : profiles) list.push(json(p));
    j["profiles"] = list;
    JJson lasts = JJson::array();
    for (const Profile& p : last) lasts.push(json(p));
    j["last"] = lasts;
    return JPWholeFile::write(path, j, error);
}

const JPMappingProfiles::Profile* JPMappingProfiles::best(const std::vector<std::string>& header,
                                                          JPImportSource::Role role) const {
    const Profile* found = nullptr;
    for (const Profile& p : profiles) {
        if (p.role != role || p.columns.empty()) continue;
        const bool fits = std::all_of(p.columns.begin(), p.columns.end(), [&header](const auto& c) {
            return std::find(header.begin(), header.end(), c.first) != header.end();
        });
        if (fits && (!found || p.columns.size() > found->columns.size())) found = &p;
    }
    return found;
}

void JPMappingProfiles::apply(const Profile& p, JPImportSource& s, const JPFields& fields) {
    s.guess(fields);
    for (size_t i = 0; i < s.table.header.size(); ++i)
        if (const auto c = p.columns.find(s.table.header[i]); c != p.columns.end()) {
            const JPImportField::Target t = JPImportField::Target::fromText(c->second);
            s.mapping[i] = !t.empty() && fields.of(t.of).has(t.key) ? t : JPImportField::Target {};
        }
    s.units = p.units;
    s.profile = p.name;
}

JPMappingProfiles::Profile JPMappingProfiles::from(const JPImportSource& s, const std::string& name) {
    Profile p;
    p.name = name;
    p.role = s.role;
    p.units = s.units;
    for (size_t i = 0; i < s.table.header.size() && i < s.mapping.size(); ++i)
        if (!s.table.header[i].empty()) p.columns[s.table.header[i]] = s.mapping[i].text();
    return p;
}

void JPMappingProfiles::put(const Profile& p) {
    for (Profile& q : profiles)
        if (q.name == p.name) {
            q = p;
            return;
        }
    profiles.push_back(p);
}

const JPMappingProfiles::Profile* JPMappingProfiles::lastFor(const std::vector<std::string>& header,
                                                             JPImportSource::Role role) const {
    for (const Profile& p : last)
        if (p.role == role && sameHeader(p, header)) return &p;
    return nullptr;
}

void JPMappingProfiles::remember(const JPImportSource& s) {
    Profile p = from(s, kLast);
    if (p.columns.empty()) return;
    for (Profile& q : last)
        if (q.role == p.role && sameHeader(q, s.table.header)) {
            q = p;
            return;
        }
    last.push_back(p);
}

} // inline namespace jf
