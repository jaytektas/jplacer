// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPPartsTableModel.h"

#include "JPLengthCell.h"

#include <j/core/JStyle.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>

inline namespace jf {

namespace {

enum Col { kId, kSource, kStatus, kDescription, kValue, kMpn, kHeight, kDepth, kPackage, kSpeed, kBottomVision,
           kFiducialVision, kPlacements, kFeeders, kColumns };

// OpenPnP's PercentConverter.
std::string percent(double d) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.1f%%", d * 100);
    return buf;
}

} // namespace

JPPartsTableModel::JPPartsTableModel(JPConfiguration& config) : m_config(config) { reload(); }

bool JPPartsTableModel::rowShown(int row) const {
    const JPCatalog::Part* e = entry(row);
    if (!e) return false;
    if (m_show == kShowAll) return true;
    if (m_show == kShowLibrary) return !e->board;
    if (m_show == kShowBoards) return e->board != nullptr;
    return e->board && e->board->scopeName() == m_show;
}

std::vector<std::string> JPPartsTableModel::showChoices() const {
    std::vector<std::string> out { kShowAll, kShowLibrary, kShowBoards };
    for (const auto& b : m_config.boards()) out.push_back(b->scopeName());
    return out;
}

void JPPartsTableModel::reload() { m_rows = JPCatalog::parts(m_config); }

int JPPartsTableModel::columnCount() const { return kColumns; }

JPTableModel::Column JPPartsTableModel::column(int c) const {
    switch (c) {
        case kId:             return { "ID", "", Kind::Text };
        case kSource:         return { "Source", "Where it lives: the library, or the open board named", Kind::Text };
        case kStatus:         return { "Status", "How it stands to the library", Kind::Text };
        case kDescription:    return { "Description", "", Kind::Text };
        case kValue:          return { "Value", "Its electrical value as written (100n, 4k7); the Library page has the rest", Kind::Text };
        case kMpn:
            return { "MPN", "Its manufacturer's part number, the first of its identifiers (the Library page has them all)",
                     Kind::Text };
        case kHeight:         return { "Height", "Part height; the distance between board surface and pick position.", Kind::Text };
        case kDepth:
            return { "Through-Board Depth",
                     "The length of any through-board features on the part. That is, the size any feature which is "
                     "below the board surface when placed. This is added to the Part Height to determine how high the "
                     "nozzle needs to be lifted when processing Dynamic Safe Z.\nThis should be zero for purely "
                     "surface-mount parts.",
                     Kind::Text };
        case kPackage:        return { "Package", "", Kind::Choice };
        case kSpeed:          return { "Speed %", "", Kind::Text };
        case kBottomVision:   return { "BottomVision", "", Kind::Choice };
        case kFiducialVision: return { "FiducialVision", "", Kind::Choice };
        case kPlacements:     return { "Placements", "", Kind::Number };
        case kFeeders:        return { "Feeders", "", Kind::Number };
    }
    return {};
}

int JPPartsTableModel::rowCount() const { return int(m_rows.size()); }

const JPCatalog::Part* JPPartsTableModel::entry(int row) const {
    return row >= 0 && size_t(row) < m_rows.size() ? &m_rows[size_t(row)] : nullptr;
}

JPPart* JPPartsTableModel::part(int row) const {
    const JPCatalog::Part* e = entry(row);
    return e ? e->part() : nullptr;
}

int JPPartsTableModel::rowOf(const JPPart* p) const {
    for (size_t i = 0; i < m_rows.size(); ++i)
        if (m_rows[i].part() == p) return int(i);
    return -1;
}

std::string JPPartsTableModel::rowKey(int row) const {
    const JPCatalog::Part* e = entry(row);
    return e ? JPCatalog::source(e->board) + "|" + e->name + "|" + JPCatalog::statusName(e->status) : std::string();
}

std::string JPPartsTableModel::text(int row, int c) const {
    const JPCatalog::Part* e = entry(row);
    if (!e) return {};
    if (c == kId) return e->name;
    if (c == kSource) return JPCatalog::source(e->board);
    if (c == kStatus) return JPCatalog::statusName(e->status);
    const JPPart* p = e->part();
    if (!p) {
        // To be chosen: what the board's files said.
        const JPBoardPart* bp = e->boardPart();
        if (c == kValue) return bp ? bp->field("value") : std::string();
        if (c == kPackage) return bp ? bp->footprintName() : std::string();
        if (c == kPlacements && bp) return std::to_string(e->board->placementsOf(bp->key).size());
        return {};
    }
    switch (c) {
        case kDescription: return p->name.value_or("");
        case kValue:       return p->value;
        case kMpn:
            for (const auto& i : p->identifiers)
                if (i.kind == "mpn") return i.code;
            return "";
        case kHeight:      return JPLengthCell::text(p->height, true);
        case kDepth:       return JPLengthCell::text(p->throughBoardDepth, true);
        case kPackage: {
            const JPPackage* k = m_config.package(p->packageId);
            if (!k) return {};
            // A board's own package by its name on the board.
            const std::string prefix = e->board ? e->board->scopeName() + "/" : std::string();
            return !prefix.empty() && k->id.rfind(prefix, 0) == 0 ? k->id.substr(prefix.size()) : k->id;
        }
        case kSpeed:       return percent(p->speed);
        case kBottomVision: {
            const JPVisionSettings* v = m_config.visionSettings(p->bottomVisionId);
            return v ? v->name : std::string();
        }
        case kFiducialVision: {
            const JPVisionSettings* v = m_config.visionSettings(p->fiducialVisionId);
            return v ? v->name : std::string();
        }
        case kPlacements: return std::to_string(m_config.placementCount(p->id));
        case kFeeders:    return std::to_string(m_config.feederCount(p->id));
    }
    return {};
}

double JPPartsTableModel::number(int row, int c) const {
    if (c == kPlacements) {
        const JPCatalog::Part* e = entry(row);
        if (e && !e->part() && e->boardPart()) return double(e->board->placementsOf(e->boardPartKey).size());
        const JPPart* p = part(row);
        return p ? m_config.placementCount(p->id) : 0;
    }
    if (c == kFeeders) {
        const JPPart* p = part(row);
        return p ? m_config.feederCount(p->id) : 0;
    }
    return 0;
}

bool JPPartsTableModel::editable(int row, int c) const {
    const JPCatalog::Part* e = entry(row);
    return e && e->part() && JPCatalog::editable(e->status) && c >= kDescription && c <= kFiducialVision && c != kMpn;
}

std::vector<const JPPackage*> JPPartsTableModel::packageChoices(int row) const {
    std::vector<const JPPackage*> out;
    for (const auto& k : m_config.packages()) out.push_back(k.get());
    if (const JPCatalog::Part* e = entry(row); e && e->board && e->status == JPCatalog::Status::Own)
        for (const JPCatalog::Package& k : JPCatalog::packages(m_config))
            if (k.board == e->board && k.status == JPCatalog::Status::Own) out.push_back(k.package());
    std::sort(out.begin(), out.end(), [](const JPPackage* a, const JPPackage* b) { return a->id < b->id; });
    return out;
}

std::string JPPartsTableModel::cellIcon(int row, int c) const {
    const JPCatalog::Part* e = entry(row);
    return e && e->board && c == kId ? "board" : std::string();
}

const uint8_t* JPPartsTableModel::cellTint(int row, int c) const {
    const JPCatalog::Part* e = entry(row);
    if (!e || !e->board) return nullptr;
    if (c == kSource) return Colors::Accent;
    if (c != kStatus) return nullptr;
    switch (e->status) {
        case JPCatalog::Status::Matched:        return Colors::Success;
        case JPCatalog::Status::LibraryChanged: return Colors::Warning;
        case JPCatalog::Status::ToBeChosen:     return Colors::Danger;
        default:                                return nullptr;
    }
}

std::string JPPartsTableModel::cellTooltip(int row, int c) const {
    const JPCatalog::Part* e = entry(row);
    if (!e) return {};
    if (c == kStatus) return JPCatalog::statusTip(e->status);
    if (c == kSource) return e->board ? "The open board " + e->board->scopeName() + "'s" : "The library's";
    return {};
}

std::vector<const JPVisionSettings*> JPPartsTableModel::visionChoices(JPVisionSettings::Kind kind) const {
    std::vector<const JPVisionSettings*> out;
    for (const JPVisionSettings& v : m_config.visionSettings())
        if (v.kind == kind) out.push_back(&v);
    std::sort(out.begin(), out.end(), [](const JPVisionSettings* a, const JPVisionSettings* b) { return a->name < b->name; });
    out.push_back(nullptr);
    return out;
}

std::vector<std::string> JPPartsTableModel::choices(int row, int c) const {
    std::vector<std::string> out;
    if (c == kPackage)
        for (const JPPackage* k : packageChoices(row)) out.push_back(k->id);
    if (c == kBottomVision || c == kFiducialVision)
        for (const JPVisionSettings* v :
             visionChoices(c == kBottomVision ? JPVisionSettings::Kind::Bottom : JPVisionSettings::Kind::Fiducial))
            out.push_back(v ? v->name : std::string());
    return out;
}

bool JPPartsTableModel::setText(int row, int c, const std::string& text, std::string& error) {
    JPPart* p = part(row);
    if (!p || !editable(row, c)) return false;
    switch (c) {
        case kDescription:
            p->name = text;
            break;
        case kValue:
            p->value = text;
            break;
        case kHeight:
        case kDepth: {
            JPLength& l = c == kHeight ? p->height : p->throughBoardDepth;
            if (!JPLengthCell::parse(text, l, l)) {
                error = "'" + text + "' is not a length";
                return false;
            }
            break;
        }
        case kSpeed: {
            std::string t;
            for (const char ch : text)
                if (ch != '%') t += ch;
            char* end = nullptr;
            const double v = std::strtod(t.c_str(), &end);
            if (end == t.c_str()) {
                error = "'" + text + "' is not a percentage";
                return false;
            }
            p->speed = v * 0.01;
            break;
        }
        default: return false;
    }
    if (onChanged) onChanged(entry(row)->board.get());
    return true;
}

void JPPartsTableModel::setChoice(int row, int c, int index) {
    JPPart* p = part(row);
    if (!p || index < 0 || !editable(row, c)) return;
    if (c == kPackage) {
        const auto ks = packageChoices(row);
        if (size_t(index) < ks.size()) p->packageId = ks[size_t(index)]->id;
    } else if (c == kBottomVision || c == kFiducialVision) {
        const auto vs = visionChoices(c == kBottomVision ? JPVisionSettings::Kind::Bottom : JPVisionSettings::Kind::Fiducial);
        if (size_t(index) >= vs.size()) return;
        (c == kBottomVision ? p->bottomVisionId : p->fiducialVisionId) = vs[size_t(index)] ? vs[size_t(index)]->id : std::string();
    } else {
        return;
    }
    if (onChanged) onChanged(entry(row)->board.get());
}

} // inline namespace jf
