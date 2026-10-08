// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPImportSource.h"

#include "model/JPLengthUnits.h"

#include <cctype>
#include <cstdlib>
#include <regex>

inline namespace jf {

namespace {

std::string upper(std::string s) {
    for (char& c : s) c = char(std::toupper(uint8_t(c)));
    return s;
}

bool endsWith(const std::string& s, const std::string& end) {
    return s.size() >= end.size() && s.compare(s.size() - end.size(), end.size(), end) == 0;
}

// The units a header or a cell names at its end ("(mil)", "mm", "in"); false when it names none.
bool unitsOf(std::string text, JPLengthUnit& u) {
    text = upper(text);
    while (!text.empty() && (text.back() == ')' || text.back() == ']' || text.back() == ' ')) text.pop_back();
    if (endsWith(text, "MILS") || endsWith(text, "MIL")) { u = JPLengthUnit::Mils; return true; }
    if (endsWith(text, "MM")) { u = JPLengthUnit::Millimeters; return true; }
    if (endsWith(text, "INCH") || endsWith(text, "IN") || endsWith(text, "\"")) { u = JPLengthUnit::Inches; return true; }
    return false;
}

// The units a tool's comment names ("Unit = inches, Angle = deg.", KiCad's .pos); false when none does.
bool unitsOfComments(const std::vector<std::string>& comments, JPLengthUnit& u) {
    static const std::regex kUnit(R"re(\bunits?\s*[=:]\s*(inches|inch|in|mils|mil|millimet(?:er|re)s?|mm)\b)re",
                                  std::regex::icase);
    for (const std::string& c : comments) {
        std::smatch m;
        if (!std::regex_search(c, m, kUnit)) continue;
        const std::string w = upper(m[1]);
        u = w.rfind("IN", 0) == 0 ? JPLengthUnit::Inches : w.rfind("MIL", 0) == 0 && w.find("MET") == std::string::npos
                                                               ? JPLengthUnit::Mils
                                                               : JPLengthUnit::Millimeters;
        return true;
    }
    return false;
}

} // namespace

const char* JPImportSource::roleName(Role r) {
    switch (r) {
        case Role::Cpl: return "cpl";
        case Role::Bom: return "bom";
        case Role::Other: break;
    }
    return "table";
}

const char* JPImportSource::roleLabel(Role r) {
    switch (r) {
        case Role::Cpl: return "Placement file (CPL)";
        case Role::Bom: return "BOM";
        case Role::Other: break;
    }
    return "Other table";
}

void JPImportSource::guess() {
    using I = JPImportField::Id;
    mapping.assign(table.header.size(), I::Ignore);
    for (size_t i = 0; i < table.header.size(); ++i) {
        I id = JPImportField::guess(table.header[i]);
        if (id != I::Extra && id != I::Ignore && column(id) >= 0) id = I::Extra;   // one column per field
        mapping[i] = id;
    }
    if (column(I::Footprint) < 0)
        if (const int k = column(I::Package); k >= 0) mapping[size_t(k)] = I::Footprint;
    // The lengths' units: X's header, else X's first cell, else what the file's comments say (KiCad's "## Unit =
    // inches"), else millimetres.
    units = JPLengthUnit::Millimeters;
    JPLengthUnit u;
    if (const int x = column(I::X); x >= 0 && unitsOf(table.header[size_t(x)], u)) units = u;
    else if (x >= 0 && !table.rows.empty() && unitsOf(table.rows[0][size_t(x)], u)) units = u;
    else if (unitsOfComments(table.comments, u)) units = u;
}

int JPImportSource::column(JPImportField::Id id) const {
    for (size_t i = 0; i < mapping.size(); ++i)
        if (mapping[i] == id) return int(i);
    return -1;
}

const std::string& JPImportSource::cell(const std::vector<std::string>& row, JPImportField::Id id) const {
    static const std::string empty;
    const int c = column(id);
    return c >= 0 && size_t(c) < row.size() ? row[size_t(c)] : empty;
}

bool JPImportSource::length(const std::string& cell, JPLengthUnit units, double& mm) {
    std::string t;
    for (const char c : cell)
        if (c != ' ') t += c;
    JPLengthUnit u = units;
    if (unitsOf(t, u)) {
        while (!t.empty() && !std::isdigit(uint8_t(t.back())) && t.back() != '.') t.pop_back();
    }
    // A decimal comma, where there is no point.
    if (t.find('.') == std::string::npos)
        for (char& c : t)
            if (c == ',') c = '.';
    if (t.empty()) return false;
    char* end = nullptr;
    const double v = std::strtod(t.c_str(), &end);
    if (!end || *end != '\0') return false;
    mm = v * JPLengthUnits::toMillimeters(u);
    return true;
}

JJson JPImportSource::provenance(const std::string& when) const {
    JJson j = JJson::object();
    j["role"] = roleName(role);
    j["file"] = table.path;
    j["when"] = when;
    if (!profile.empty()) j["profile"] = profile;
    j["units"] = JPLengthUnits::name(units);
    JJson cols = JJson::array();
    for (size_t i = 0; i < table.header.size(); ++i) {
        JJson c = JJson::object();
        c["header"] = table.header[i];
        c["field"] = JPImportField::key(i < mapping.size() ? mapping[i] : JPImportField::Id::Ignore);
        cols.push(c);
    }
    j["columns"] = cols;
    JJson rows = JJson::array();
    for (const auto& r : table.rows) {
        JJson row = JJson::array();
        for (const std::string& c : r) row.push(JJson(c));
        rows.push(row);
    }
    j["rows"] = rows;
    return j;
}

} // inline namespace jf
