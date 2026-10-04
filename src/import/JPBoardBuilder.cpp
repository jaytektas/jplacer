// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPBoardBuilder.h"

#include "JPBomImporter.h"
#include "JPCplImporter.h"
#include "JPCsvTable.h"

#include <algorithm>
#include <filesystem>
#include <map>

inline namespace jf {

namespace {

std::string numbersText(const std::vector<JPSupplierNumber>& ns) {
    std::string out;
    for (const JPSupplierNumber& n : ns) out += (out.empty() ? "" : "; ") + (n.supplier.empty() ? n.number : n.supplier + " " + n.number);
    return out;
}

bool same(const std::string& a, const std::string& b) {
    return JPCsvTable::lower(JPCsvTable::trim(a)) == JPCsvTable::lower(JPCsvTable::trim(b));
}

// A text field: the BOM's fills an empty one; two different ones disagree.
void join(JPPlacement& p, std::string JPPlacement::*field, const JPPlacement& bom, const char* name,
          const std::set<std::string>& takeBom, JPBoardBuilder::Result& out) {
    std::string& mine = p.*field;
    const std::string& theirs = bom.*field;
    if (theirs.empty() || same(mine, theirs)) return;
    if (mine.empty()) {
        mine = theirs;
        return;
    }
    JPBoardBuilder::Disagreement d { p.designator, name, mine, theirs, false };
    d.takeBom = takeBom.count(d.key()) != 0;
    if (d.takeBom) mine = theirs;
    out.disagreements.push_back(std::move(d));
}

void joinBom(JPPlacement& p, const JPPlacement& bom, const std::set<std::string>& takeBom, JPBoardBuilder::Result& out) {
    join(p, &JPPlacement::value, bom, "Value", takeBom, out);
    join(p, &JPPlacement::mpn, bom, "MPN", takeBom, out);
    join(p, &JPPlacement::manufacturer, bom, "Manufacturer", takeBom, out);
    join(p, &JPPlacement::footprint, bom, "Footprint", takeBom, out);
    // Supplier numbers as one field.
    if (!bom.supplierNumbers.empty()) {
        const std::string mine = numbersText(p.supplierNumbers), theirs = numbersText(bom.supplierNumbers);
        if (p.supplierNumbers.empty()) {
            p.supplierNumbers = bom.supplierNumbers;
        } else if (!same(mine, theirs)) {
            JPBoardBuilder::Disagreement d { p.designator, "Supplier numbers", mine, theirs, false };
            d.takeBom = takeBom.count(d.key()) != 0;
            if (d.takeBom) p.supplierNumbers = bom.supplierNumbers;
            out.disagreements.push_back(std::move(d));
        }
    }
    // Placed or not.
    if (bom.doNotPlace != p.doNotPlace && bom.doNotPlace) {
        JPBoardBuilder::Disagreement d { p.designator, "Placed", "placed", "do not place", false };
        d.takeBom = takeBom.count(d.key()) != 0;
        if (d.takeBom) p.doNotPlace = true;
        out.disagreements.push_back(std::move(d));
    }
    // The rest only fill what the file left empty.
    for (std::string JPPlacement::*f : { &JPPlacement::tolerance, &JPPlacement::voltage, &JPPlacement::power,
                                         &JPPlacement::dielectric, &JPPlacement::temperature, &JPPlacement::supplierPackage,
                                         &JPPlacement::description, &JPPlacement::device, &JPPlacement::datasheet })
        if ((p.*f).empty()) p.*f = bom.*f;
    if (p.pins == 0) p.pins = bom.pins;
    if (p.mounting == JPPlacement::Mounting::Unknown) p.mounting = bom.mounting;
    for (const auto& o : bom.other)
        if (std::none_of(p.other.begin(), p.other.end(), [&o](const auto& m) { return m.first == o.first; }))
            p.other.push_back(o);
}

} // namespace

bool JPBoardBuilder::build(const std::vector<JPSource>& sources, const JPBoardFrame& frame,
                           const std::set<std::string>& takeBom, Result& out, std::string& error) {
    out = Result();
    const JPSource* cpl = nullptr;
    const JPSource* bom = nullptr;
    for (const JPSource& s : sources) {
        if (s.kind == JPSource::Kind::Placements && !cpl) cpl = &s;
        if (s.kind == JPSource::Kind::Bom && !bom) bom = &s;
    }
    if (!cpl) {
        error = "no pick-and-place file";
        return false;
    }
    if (!JPCplImporter::read(cpl->path, out.board, out.notes, error)) return false;

    // The same designator twice: refused.
    std::map<std::string, int> seen;
    for (const JPPlacement& p : out.board.placements) ++seen[p.designator];
    std::vector<std::string> twice;
    for (const auto& [d, n] : seen)
        if (n > 1) twice.push_back(d);
    if (!twice.empty()) {
        error = cpl->path + ": these designators are there more than once, and placements are told apart by designator:";
        for (const std::string& d : twice) error += " " + d;
        return false;
    }

    // Into the one frame. Fiducials start as references.
    for (JPPlacement& p : out.board.placements) {
        p.reference = p.fiducial;
        if (cpl->tool == JPCadTool::Kind::KiCadNegativeX && p.side == JPPlacement::Side::Bottom) {
            p.x = -p.x;
            p.pin1X = -p.pin1X;
        }
        p.x -= frame.originX;
        p.y -= frame.originY;
        p.pin1X -= frame.originX;
        p.pin1Y -= frame.originY;
    }

    if (bom) {
        std::vector<JPBomImporter::Line> lines;
        std::vector<std::string> notes;
        if (!JPBomImporter::read(bom->path, lines, notes, error)) return false;
        for (const std::string& n : notes) out.notes.push_back("BOM: " + n);
        std::map<std::string, const JPPlacement*> byDesignator;
        for (const JPBomImporter::Line& l : lines)
            for (const std::string& d : l.designators) byDesignator[d] = &l.part;
        for (JPPlacement& p : out.board.placements) {
            const auto it = byDesignator.find(p.designator);
            if (it == byDesignator.end()) {
                if (!p.fiducial) out.noBomLine.push_back(p.designator);
                continue;
            }
            joinBom(p, *it->second, takeBom, out);
        }
        for (const auto& [d, part] : byDesignator)
            if (!out.board.find(d)) out.bomOnly.push_back(d);
    }
    out.board.name = std::filesystem::path(cpl->path).stem().string();
    return true;
}

} // inline namespace jf
