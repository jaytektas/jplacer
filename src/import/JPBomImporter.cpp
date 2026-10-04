// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPBomImporter.h"

#include "JPCsvTable.h"

#include <fstream>
#include <sstream>

inline namespace jf {

namespace {

std::vector<std::string> designatorsIn(const std::string& cell) {
    std::vector<std::string> out;
    std::string d;
    for (const char c : cell + ",") {
        if (c == ',' || c == ';' || c == ' ' || c == '\t') {
            if (!d.empty()) out.push_back(d);
            d.clear();
        } else {
            d += c;
        }
    }
    return out;
}

} // namespace

bool JPBomImporter::parse(const std::string& text, std::vector<Line>& out, std::vector<std::string>& notes, std::string& error) {
    using Column = JPCsvTable::Column;
    JPCsvTable t;
    if (!t.parse(text, [](const JPCsvTable& h) { return h.has(Column::Designator); }, error)) {
        error = "no heading row naming a designator column";
        return false;
    }
    int empty = 0;
    for (size_t i = 0; i < t.records().size(); ++i) {
        const JPCsvTable::Record& rec = t.records()[i];
        Line l;
        l.designators = designatorsIn(t.field(rec, Column::Designator));
        l.line = t.lines()[i];
        if (l.designators.empty()) {
            ++empty;
            continue;
        }
        t.partFields(rec, l.part);
        out.push_back(std::move(l));
    }
    if (empty) notes.push_back(std::to_string(empty) + " BOM line(s) without a designator were left out");
    if (out.empty()) {
        error = "no lines with designators";
        return false;
    }
    return true;
}

bool JPBomImporter::read(const std::string& path, std::vector<Line>& out, std::vector<std::string>& notes, std::string& error) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        error = "cannot open " + path;
        return false;
    }
    std::stringstream ss;
    ss << in.rdbuf();
    if (!parse(ss.str(), out, notes, error)) {
        error = path + ": " + error;
        return false;
    }
    return true;
}

} // inline namespace jf
