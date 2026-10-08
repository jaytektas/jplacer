// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPDesignators.h"

#include <algorithm>
#include <cctype>
#include <cstdint>

inline namespace jf {

namespace {

constexpr int kLongestRange = 10000;   // a range longer than this is taken as two names, not expanded

// "R12" as "R" and 12; false when it does not end in a number.
bool numbered(const std::string& d, std::string& prefix, long& n) {
    size_t i = d.size();
    while (i > 0 && std::isdigit(uint8_t(d[i - 1]))) --i;
    if (i == d.size()) return false;
    prefix = d.substr(0, i);
    n = std::stol(d.substr(i));
    return true;
}

} // namespace

std::vector<std::string> JPDesignators::expand(const std::string& text) {
    std::vector<std::string> tokens;
    std::string t;
    for (const char c : text) {
        if (c == ',' || c == ';' || std::isspace(uint8_t(c))) {
            if (!t.empty()) tokens.push_back(t);
            t.clear();
        } else {
            t += c;
        }
    }
    if (!t.empty()) tokens.push_back(t);

    std::vector<std::string> out;
    auto add = [&out](const std::string& d) {
        if (!d.empty() && std::find(out.begin(), out.end(), d) == out.end()) out.push_back(d);
    };
    for (const std::string& tok : tokens) {
        const size_t dash = tok.find('-', 1);
        std::string p1, p2;
        long a = 0, b = 0;
        if (dash != std::string::npos && numbered(tok.substr(0, dash), p1, a)) {
            std::string end = tok.substr(dash + 1);
            if (!end.empty() && std::isdigit(uint8_t(end[0]))) end = p1 + end;   // "R5-8"
            if (numbered(end, p2, b) && p1 == p2 && b >= a && b - a <= kLongestRange) {
                for (long n = a; n <= b; ++n) add(p1 + std::to_string(n));
                continue;
            }
        }
        add(tok);
    }
    return out;
}

} // inline namespace jf
