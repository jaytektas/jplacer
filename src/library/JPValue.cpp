// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPValue.h"

#include <cctype>
#include <cmath>
#include <cstdlib>

inline namespace jf {

namespace {

std::string lower(const std::string& s) {
    std::string out;
    for (const char c : s)
        if (!std::isspace(static_cast<unsigned char>(c))) out += char(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

// The multiplier an SI prefix letter stands for; 0 when it is not one.
double prefix(char c) {
    switch (c) {
        case 'p': return 1e-12;
        case 'n': return 1e-9;
        case 'u': return 1e-6;
        case 'm': return 1e-3;
        case 'k': return 1e3;
        case 'g': return 1e9;
        default:  return 0;
    }
}

bool endsWith(const std::string& s, const std::string& tail) {
    return s.size() >= tail.size() && s.compare(s.size() - tail.size(), tail.size(), tail) == 0;
}

} // namespace

bool JPValue::parse(const std::string& textIn, JPValue& out) {
    std::string t = lower(textIn);
    // µ (U+00B5) and μ (U+03BC) are micro; Ω (U+2126 or U+03A9) is ohm.
    for (const char* micro : { "\xC2\xB5", "\xCE\xBC" })
        for (size_t at = t.find(micro); at != std::string::npos; at = t.find(micro)) t.replace(at, 2, "u");
    for (const char* omega : { "\xE2\x84\xA6", "\xCE\xA9", "\xCF\x89" })
        for (size_t at = t.find(omega); at != std::string::npos; at = t.find(omega)) t.replace(at, std::string(omega).size(), "ohm");
    if (t.empty()) return false;

    Unit unit = Unit::None;
    for (const char* name : { "ohms", "ohm" })
        if (endsWith(t, name)) { unit = Unit::Ohm; t.erase(t.size() - std::string(name).size()); break; }
    if (unit == Unit::None && endsWith(t, "f")) { unit = Unit::Farad; t.pop_back(); }
    else if (unit == Unit::None && endsWith(t, "h")) { unit = Unit::Henry; t.pop_back(); }
    if (t.empty()) return false;

    // A letter between digits is the decimal point and the multiplier: 4k7, 4R7, 2n2.
    for (size_t i = 1; i + 1 < t.size(); ++i) {
        const char c = t[i];
        if (!std::isdigit(static_cast<unsigned char>(t[i - 1])) || !std::isdigit(static_cast<unsigned char>(t[i + 1]))) continue;
        if (c == 'r' || prefix(c) > 0) {
            const double whole = std::strtod(t.substr(0, i).c_str(), nullptr);
            const std::string frac = t.substr(i + 1);
            for (const char d : frac)
                if (!std::isdigit(static_cast<unsigned char>(d))) return false;
            const double v = whole + std::strtod(("0." + frac).c_str(), nullptr);
            if (c == 'r') { out.number = v; out.unit = unit == Unit::None ? Unit::Ohm : unit; }
            else          { out.number = v * (c == 'm' && unit == Unit::None ? 1e6 : prefix(c)); out.unit = unit; }
            return true;
        }
    }
    char* end = nullptr;
    const double v = std::strtod(t.c_str(), &end);
    if (end == t.c_str()) return false;
    std::string rest(end);
    double scale = 1;
    if (rest == "r") { rest.clear(); if (unit == Unit::None) unit = Unit::Ohm; }
    else if (rest == "meg") { rest.clear(); scale = 1e6; }
    else if (rest.size() == 1 && rest[0] == 'm' && unit == Unit::None) { rest.clear(); scale = 1e6; }   // 1M: a resistor's mega
    else if (rest.size() == 1 && prefix(rest[0]) > 0) { scale = prefix(rest[0]); rest.clear(); }
    if (!rest.empty()) return false;
    out.number = v * scale;
    out.unit = unit;
    return true;
}

bool JPValue::same(const std::string& a, const std::string& b) {
    JPValue va, vb;
    if (parse(a, va) && parse(b, vb)) {
        if (va.unit != vb.unit) return false;
        const double scale = std::max(std::abs(va.number), std::abs(vb.number));
        return std::abs(va.number - vb.number) <= scale * 1e-9;
    }
    return lower(a) == lower(b);
}

} // inline namespace jf
