// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPValueRegex.h"

inline namespace jf {

bool JPValueRegex::compile(const std::string& pattern) {
    // Each capturing group counted in order: "(" not escaped, outside a character class, not "(?:", "(?=",
    // "(?!", "(?<=" or "(?<!"; a named one "(?<Name>" is one too, and is written as a plain "(".
    std::string plain;
    plain.reserve(pattern.size());
    m_valueGroup.reset();
    size_t groups = 0;
    bool inClass = false;
    for (size_t i = 0; i < pattern.size(); ++i) {
        const char c = pattern[i];
        if (c == '\\' && i + 1 < pattern.size()) {
            plain += c;
            plain += pattern[++i];
            continue;
        }
        if (inClass) {
            if (c == ']') inClass = false;
            plain += c;
            continue;
        }
        if (c == '[') {
            inClass = true;
            plain += c;
            continue;
        }
        if (c != '(') {
            plain += c;
            continue;
        }
        if (pattern.compare(i, 3, "(?<") == 0 && i + 3 < pattern.size() && pattern[i + 3] != '=' && pattern[i + 3] != '!') {
            const size_t close = pattern.find('>', i + 3);
            if (close == std::string::npos) return false;
            ++groups;
            if (pattern.compare(i + 3, close - i - 3, "Value") == 0) m_valueGroup = groups;
            plain += '(';
            i = close;
            continue;
        }
        if (i + 1 >= pattern.size() || pattern[i + 1] != '?') ++groups;
        plain += c;
    }
    try {
        m_regex = std::regex(plain);
    } catch (const std::regex_error&) {
        return false;
    }
    return true;
}

} // inline namespace jf
