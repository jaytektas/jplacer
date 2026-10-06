// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPUnitText.h"

#include <cctype>
#include <cstdlib>

inline namespace jf {

namespace {

std::string trimmed(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

} // namespace

JPUnitText::Parts JPUnitText::split(const std::string& text, int power) {
    const std::string s = trimmed(text);
    Parts p;
    size_t startOfUnits = std::string::npos;
    for (size_t i = 0; i < s.size(); ++i) {
        const char ch = s[i];
        if (ch != '-' && ch != '.' && !std::isdigit(static_cast<unsigned char>(ch))) {
            startOfUnits = i;
            break;
        }
    }
    if (startOfUnits == std::string::npos) {
        p.value = s;
        return p;
    }
    p.value = s.substr(0, startOfUnits);
    p.hasUnits = true;
    for (const char c : trimmed(s.substr(startOfUnits))) {
        if (c == 'u') p.units += "\xCE\xBC";
        else if (power == 2 && c == '2') p.units += "\xC2\xB2";
        else if (power == 3 && c == '3') p.units += "\xC2\xB3";
        else p.units += c;
    }
    return p;
}

std::optional<double> JPUnitText::number(const std::string& value) {
    const std::string v = trimmed(value);
    if (v.empty()) return std::nullopt;
    char* end = nullptr;
    const double d = std::strtod(v.c_str(), &end);
    if (end != v.c_str() + v.size()) return std::nullopt;
    return d;
}

bool JPUnitText::sameIgnoringCase(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i]))) return false;
    return true;
}

} // inline namespace jf
