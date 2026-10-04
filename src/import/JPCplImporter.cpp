// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPCplImporter.h"

#include "JPCsvTable.h"

#include "common/JPlacerLog.h"

#include <j/core/Log.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

inline namespace jf {

namespace {

using Column = JPCsvTable::Column;

// The first size in mm a footprint's name gives ("FIDUCIAL_1MM",
// "Fiducial_0.75mm_Mask1.5mm": the copper comes first); 0 when none.
double sizeInName(const std::string& footprint) {
    const std::string f = JPCsvTable::lower(footprint);
    for (size_t at = f.find("mm"); at != std::string::npos; at = f.find("mm", at + 2)) {
        size_t b = at;
        while (b > 0 && (std::isdigit(static_cast<unsigned char>(f[b - 1])) || f[b - 1] == '.')) --b;
        if (b < at) return std::strtod(f.c_str() + b, nullptr);
    }
    return 0;
}

bool startsWith(const std::string& s, const char* prefix) {
    return JPCsvTable::lower(s).rfind(prefix, 0) == 0;
}

} // namespace

bool JPCplImporter::read(const std::string& path, JPBoard& board, std::vector<std::string>& notes, std::string& error) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        error = "cannot open " + path;
        return false;
    }
    std::stringstream ss;
    ss << in.rdbuf();
    if (!parse(ss.str(), board, notes, error)) {
        error = path + ": " + error;
        return false;
    }
    if (board.name.empty()) board.name = std::filesystem::path(path).stem().string();
    JLOGC(JPlacerLog::kImportCpl, JLogLevel::Info) << path << ": " << board.placements.size() << " placement(s)";
    return true;
}

bool JPCplImporter::parse(const std::string& text, JPBoard& board, std::vector<std::string>& notes, std::string& error) {
    // The heading row: the first that names a designator and both coordinates.
    JPCsvTable t;
    const bool ok = t.parse(text, [](const JPCsvTable& h) {
        return h.has(Column::Designator) && (h.has(Column::X) || h.has(Column::PadX)) && (h.has(Column::Y) || h.has(Column::PadY));
    }, error);
    if (!ok) {
        error = "no heading row naming a designator and X and Y columns";
        return false;
    }
    // Pad 1 is the position only when the file gives nothing better.
    const Column xCol = t.has(Column::X) ? Column::X : Column::PadX;
    const Column yCol = t.has(Column::Y) ? Column::Y : Column::PadY;
    if (!t.has(Column::Side)) notes.push_back("no side column: every placement is taken to be on the top");
    if (!t.has(Column::Rotation)) notes.push_back("no rotation column: every placement is taken to be unturned");

    int skipped = 0;
    for (const JPCsvTable::Record& rec : t.records()) {
        JPPlacement p;
        p.designator = t.field(rec, Column::Designator);
        if (p.designator.empty() || !JPCsvTable::length(t.field(rec, xCol), t.heading(xCol).unitsPerMm, p.x)
            || !JPCsvTable::length(t.field(rec, yCol), t.heading(yCol).unitsPerMm, p.y)) {
            ++skipped;
            continue;
        }
        if (const std::string r = t.field(rec, Column::Rotation); !r.empty()) p.rotationDeg = std::strtod(r.c_str(), nullptr);
        if (const std::string s = t.field(rec, Column::Side); !s.empty() && !JPCsvTable::sideOf(s, p.side))
            notes.push_back(p.designator + ": side '" + s + "' not understood; taken as top");
        if (t.has(Column::PadX) && t.has(Column::PadY))
            p.hasPin1 = JPCsvTable::length(t.field(rec, Column::PadX), t.heading(Column::PadX).unitsPerMm, p.pin1X)
                     && JPCsvTable::length(t.field(rec, Column::PadY), t.heading(Column::PadY).unitsPerMm, p.pin1Y);
        t.partFields(rec, p);
        p.fiducial  = startsWith(p.designator, "fid") || JPCsvTable::lower(p.footprint).find("fiducial") != std::string::npos;
        if (p.fiducial) p.fiducialMm = sizeInName(p.footprint);
        board.placements.push_back(std::move(p));
    }
    if (skipped) notes.push_back(std::to_string(skipped) + " row(s) without a designator and position were left out");
    if (board.placements.empty()) {
        error = "no placements";
        return false;
    }
    return true;
}

} // inline namespace jf
