// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPField.h"

#include "JPLength.h"

#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>

inline namespace jf {

namespace {

std::string trim(const std::string& s) {
    size_t b = 0, e = s.size();
    while (b < e && uint8_t(s[b]) <= ' ') ++b;
    while (e > b && uint8_t(s[e - 1]) <= ' ') --e;
    return s.substr(b, e - b);
}

std::string upper(std::string s) {
    for (char& c : s) c = char(std::toupper(uint8_t(c)));
    return s;
}

// A heading made comparable: upper case, letters and digits only ("#" a NUMBER).
std::string normalised(const std::string& heading) {
    std::string s;
    for (const unsigned char c : heading)
        if (std::isalnum(c)) s += char(std::toupper(c));
        else if (c == '#') s += "NUMBER";
    return s;
}

// A number as short as it reads ("12", "0.5", "1e-05").
std::string shortNumber(double v) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.10g", v);
    return buf;
}

} // namespace

const std::vector<JPField::Type>& JPField::types() {
    static const std::vector<Type> v { Type::Text, Type::Number, Type::Length, Type::YesNo, Type::Choice };
    return v;
}

const char* JPField::typeKey(Type t) {
    switch (t) {
        case Type::Text:   return "text";
        case Type::Number: return "number";
        case Type::Length: return "length";
        case Type::YesNo:  return "yesNo";
        case Type::Choice: return "choice";
    }
    return "text";
}

const char* JPField::typeLabel(Type t) {
    switch (t) {
        case Type::Text:   return "Text";
        case Type::Number: return "Number";
        case Type::Length: return "Length";
        case Type::YesNo:  return "Yes/No";
        case Type::Choice: return "Choice";
    }
    return "Text";
}

JPField::Type JPField::typeFrom(const std::string& key) {
    for (const Type t : types())
        if (key == typeKey(t)) return t;
    return Type::Text;
}

std::string JPField::text(const JJson& v) const {
    if (v.isNull()) return {};
    switch (type) {
        case Type::Number:
        case Type::Length: return v.isNumber() ? shortNumber(v.number()) : v.isString() ? v.str() : std::string();
        case Type::YesNo:  return v.isBool() ? (v.boolean() ? "Yes" : "") : v.isString() ? v.str() : std::string();
        case Type::Text:
        case Type::Choice:
            if (v.isString()) return v.str();
            if (v.isNumber()) return shortNumber(v.number());
            if (v.isBool()) return v.boolean() ? "Yes" : "No";
            return {};
    }
    return {};
}

bool JPField::parse(const std::string& raw, JJson& v, std::string& error, JPLengthUnit unitless) const {
    const std::string t = trim(raw);
    if (t.empty()) {
        v = JJson();
        return true;
    }
    switch (type) {
        case Type::Text:
            v = JJson(t);
            return true;
        case Type::Number: {
            std::string s = t;
            if (s.find('.') == std::string::npos)
                for (char& c : s)
                    if (c == ',') c = '.';   // "12,5" as it is written in much of the world
            char* end = nullptr;
            const double d = std::strtod(s.c_str(), &end);
            if (!end || *end || !std::isfinite(d)) {
                error = "'" + t + "' is not a number";
                return false;
            }
            v = JJson(d);
            return true;
        }
        case Type::Length: {
            const std::optional<JPLength> l = JPLength::parseWithDefaultUnits(t, unitless);
            if (!l) {
                error = "'" + t + "' is not a length";
                return false;
            }
            v = JJson(l->convertToUnits(JPLengthUnit::Millimeters).value());
            return true;
        }
        case Type::YesNo: {
            const std::string u = upper(t);
            if (u == "YES" || u == "Y" || u == "TRUE" || u == "1" || u == "X" || u == "ON") v = JJson(true);
            else if (u == "NO" || u == "N" || u == "FALSE" || u == "0" || u == "OFF") v = JJson(false);
            else {
                error = "'" + t + "' is not Yes or No";
                return false;
            }
            return true;
        }
        case Type::Choice:
            for (const std::string& c : choices)
                if (upper(c) == upper(t)) {
                    v = JJson(c);
                    return true;
                }
            error = "'" + t + "' is not one of " + name + "'s choices";
            return false;
    }
    return false;
}

bool JPField::matchesHeading(const std::string& heading) const {
    const std::string n = normalised(heading);
    if (n.empty()) return false;
    // A units suffix ("Height (mm)") names how it is measured, not what.
    std::vector<std::string> forms { n };
    for (const char* unit : { "MILS", "MIL", "MM", "INCH", "IN" }) {
        const std::string u = unit;
        if (n.size() > u.size() && n.compare(n.size() - u.size(), u.size(), u) == 0) forms.push_back(n.substr(0, n.size() - u.size()));
    }
    for (const std::string& m : matches) {
        const std::string want = normalised(m);
        for (const std::string& f : forms)
            if (f == want) return true;
    }
    return normalised(name) == n;
}

JJson JPField::toJson() const {
    JJson j = JJson::object();
    j["key"] = key;
    j["name"] = name;
    if (!builtIn) j["type"] = typeKey(type);
    if (type == Type::Choice) {
        JJson c = JJson::array();
        for (const std::string& s : choices) c.push(JJson(s));
        j["choices"] = c;
    }
    JJson m = JJson::array();
    for (const std::string& s : matches) m.push(JJson(s));
    j["matches"] = m;
    if (removed) j["removed"] = true;
    return j;
}

JPField JPField::fromJson(const JJson& j) {
    JPField f;
    f.key = j["key"].str();
    f.name = j["name"].str();
    f.type = typeFrom(j["type"].str());
    for (const JJson& c : j["choices"].arr())
        if (c.isString()) f.choices.push_back(c.str());
    for (const JJson& m : j["matches"].arr())
        if (m.isString()) f.matches.push_back(m.str());
    f.removed = j["removed"].boolean(false);
    return f;
}

} // inline namespace jf
