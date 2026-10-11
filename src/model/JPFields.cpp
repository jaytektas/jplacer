// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPFields.h"

#include "common/JPWholeFile.h"

#include <filesystem>

inline namespace jf {

JPFields::JPFields() {
    for (const JPFieldSet::Of of : JPFieldSet::kinds()) m_sets.emplace_back(of);
}

JPFieldSet& JPFields::of(JPFieldSet::Of of) {
    for (JPFieldSet& s : m_sets)
        if (s.of() == of) return s;
    return m_sets.front();
}

const JPFieldSet& JPFields::of(JPFieldSet::Of of) const {
    for (const JPFieldSet& s : m_sets)
        if (s.of() == of) return s;
    return m_sets.front();
}

bool JPFields::load(const std::string& path, std::string& error) {
    m_sets.clear();
    for (const JPFieldSet::Of of : JPFieldSet::kinds()) m_sets.emplace_back(of);
    changed();
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) return true;
    const std::optional<JJson> j = JJson::tryParseFile(path);
    if (!j || !j->isObject() || (*j)["format"].str() != kFormat) {
        error = path + " is not jplacer's fields file";
        return false;
    }
    for (JPFieldSet& s : m_sets) s.fromJson((*j)[JPFieldSet::key(s.of())]);
    return true;
}

bool JPFields::save(const std::string& path, std::string& error) const {
    JJson j = JJson::object();
    j["format"] = kFormat;
    j["version"] = 1;
    for (const JPFieldSet& s : m_sets) j[JPFieldSet::key(s.of())] = s.toJson();
    return JPWholeFile::write(path, j, error);
}

} // inline namespace jf
