// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include <j/config/Json.h>

#include <map>
#include <string>

inline namespace jf {

// What a thing (a part, a placement, a feeder, a board…) holds in the fields added to its kind (JPField), by
// each field's key; a field it has no value in is not here. Kept with the thing in its file, under "custom".
// A value whose field is gone stays as it is (a field's key is never given again), so a file read without
// its fields known loses nothing.
class JPFieldValues {
public:
    // The value in `key`, or null.
    const JJson& get(const std::string& key) const;
    // `value` kept in `key`; a null value takes it out.
    void set(const std::string& key, const JJson& value);
    void erase(const std::string& key) { m_values.erase(key); }
    bool has(const std::string& key) const { return m_values.count(key) > 0; }
    bool empty() const { return m_values.empty(); }
    const std::map<std::string, JJson>& all() const { return m_values; }

    // As kept: an object of the values by key.
    JJson toJson() const;
    static JPFieldValues fromJson(const JJson& j);
    // `values` kept in `j` under "custom" (when there are any); read back from it.
    static void write(JJson& j, const JPFieldValues& values);
    static JPFieldValues read(const JJson& j) { return fromJson(j["custom"]); }

    bool operator==(const JPFieldValues& o) const;

private:
    std::map<std::string, JJson> m_values;
};

} // inline namespace jf
