// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPCsvLine.h"

inline namespace jf {

std::vector<std::string> JPCsvLine::split(const std::string& line, char separator) {
    auto blank = [separator](char c) { return c != separator && (c == ' ' || c == '\t' || c == '\r' || c == '\n'); };
    std::vector<std::string> out;
    size_t i = 0;
    bool any = false;
    for (char c : line)
        if (!blank(c)) any = true;
    if (!any) return out;
    for (;;) {
        while (i < line.size() && blank(line[i])) ++i;
        std::string field;
        if (i < line.size() && line[i] == '"') {
            for (++i; i < line.size() && line[i] != '"'; ++i) {
                if (line[i] == '\\' && i + 1 < line.size()) {
                    const char e = line[++i];
                    field += e == 'n' ? '\n' : e == 'r' ? '\r' : e == 't' ? '\t' : e;
                } else {
                    field += line[i];
                }
            }
            while (i < line.size() && line[i] != separator) ++i;
        } else {
            while (i < line.size() && line[i] != separator) field += line[i++];
            while (!field.empty() && blank(field.back())) field.pop_back();
        }
        out.push_back(field);
        if (i >= line.size()) break;
        ++i;   // the separator
    }
    return out;
}

} // inline namespace jf
