// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPMappingProfiles.h"

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

} // namespace

bool JPMappingProfiles::load(const std::string& path, std::string& error) {
    profiles.clear();
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) return true;
    const std::optional<JJson> j = JJson::tryParseFile(path);
    if (!j || !(*j)["profiles"].isArray()) {
        error = "Not a file of import profiles: " + path;
        return false;
    }
    for (const JJson& p : (*j)["profiles"].arr()) {
        Profile out;
        if (p["name"].isString()) out.name = p["name"].str();
        out.role = roleFrom(p["role"].isString() ? p["role"].str() : std::string());
        if (p["units"].isString()) JPLengthUnits::fromName(p["units"].str(), out.units);
        if (p["columns"].isObject())
            for (const auto& [header, field] : p["columns"].obj())
                if (field.isString()) out.columns[header] = field.str();
        if (!out.name.empty()) profiles.push_back(out);
    }
    return true;
}

bool JPMappingProfiles::save(const std::string& path, std::string& error) const {
    JJson j = JJson::object();
    JJson list = JJson::array();
    for (const Profile& p : profiles) {
        JJson o = JJson::object();
        o["name"] = p.name;
        o["role"] = JPImportSource::roleName(p.role);
        o["units"] = JPLengthUnits::name(p.units);
        JJson cols = JJson::object();
        for (const auto& [header, field] : p.columns) cols[header] = field;
        o["columns"] = cols;
        list.push(o);
    }
    j["profiles"] = list;
    if (!j.dumpToFile(path)) {
        error = "Cannot write " + path;
        return false;
    }
    return true;
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

void JPMappingProfiles::apply(const Profile& p, JPImportSource& s) {
    s.guess();
    for (size_t i = 0; i < s.table.header.size(); ++i)
        if (const auto c = p.columns.find(s.table.header[i]); c != p.columns.end())
            s.mapping[i] = JPImportField::fromKey(c->second);
    s.units = p.units;
    s.profile = p.name;
}

JPMappingProfiles::Profile JPMappingProfiles::from(const JPImportSource& s, const std::string& name) {
    Profile p;
    p.name = name;
    p.role = s.role;
    p.units = s.units;
    for (size_t i = 0; i < s.table.header.size() && i < s.mapping.size(); ++i)
        if (!s.table.header[i].empty()) p.columns[s.table.header[i]] = JPImportField::key(s.mapping[i]);
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

} // inline namespace jf
