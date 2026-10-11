// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPFieldValues.h"

inline namespace jf {

const JJson& JPFieldValues::get(const std::string& key) const {
    static const JJson none;
    const auto it = m_values.find(key);
    return it == m_values.end() ? none : it->second;
}

void JPFieldValues::set(const std::string& key, const JJson& value) {
    if (value.isNull()) m_values.erase(key);
    else m_values[key] = value;
}

JJson JPFieldValues::toJson() const {
    JJson j = JJson::object();
    for (const auto& [k, v] : m_values) j[k] = v;
    return j;
}

JPFieldValues JPFieldValues::fromJson(const JJson& j) {
    JPFieldValues out;
    for (const auto& [k, v] : j.obj())
        if (!v.isNull()) out.m_values[k] = v;
    return out;
}

void JPFieldValues::write(JJson& j, const JPFieldValues& values) {
    if (!values.empty()) j["custom"] = values.toJson();
}

bool JPFieldValues::operator==(const JPFieldValues& o) const {
    if (m_values.size() != o.m_values.size()) return false;
    for (const auto& [k, v] : m_values) {
        const auto it = o.m_values.find(k);
        if (it == o.m_values.end() || it->second.dump() != v.dump()) return false;
    }
    return true;
}

} // inline namespace jf
