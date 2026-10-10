// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPPackagesTableModel.h"

#include "JPSourceColumn.h"

#include <j/core/JStyle.h>

#include <algorithm>

inline namespace jf {

namespace {
enum Col { kSource, kId, kStatus, kDescription, kTape, kBottomVision, kFiducialVision, kColumns };
}

JPPackagesTableModel::JPPackagesTableModel(JPConfiguration& config) : m_config(config) { reload(); }

bool JPPackagesTableModel::rowShown(int row) const {
    const JPCatalog::Package* e = entry(row);
    if (!e) return false;
    if (m_show == kShowAll) return true;
    if (m_show == kShowLibrary) return !e->board;
    if (m_show == kShowBoards) return e->board != nullptr;
    return e->board && e->board->scopeName() == m_show;
}

std::vector<std::string> JPPackagesTableModel::showChoices() const {
    std::vector<std::string> out { kShowAll, kShowLibrary, kShowBoards };
    for (const auto& b : m_config.boards()) out.push_back(b->scopeName());
    return out;
}

void JPPackagesTableModel::reload() { m_rows = JPCatalog::packages(m_config); }

int JPPackagesTableModel::columnCount() const { return kColumns; }

bool JPPackagesTableModel::editable(int row, int c) const {
    const JPCatalog::Package* e = entry(row);
    return e && JPCatalog::editable(e->status) && c >= kDescription;
}

std::string JPPackagesTableModel::cellIcon(int row, int c) const {
    const JPCatalog::Package* e = entry(row);
    return e && c == kSource ? (e->board ? "board-source" : "library") : std::string();
}

const uint8_t* JPPackagesTableModel::cellTint(int row, int c) const {
    const JPCatalog::Package* e = entry(row);
    if (!e || !e->board || c != kStatus) return nullptr;
    switch (e->status) {
        case JPCatalog::Status::Matched:        return Colors::Success;
        case JPCatalog::Status::LibraryChanged: return Colors::Warning;
        default:                                return nullptr;
    }
}

std::string JPPackagesTableModel::displayText(int row, int c) const {
    return c == kSource ? std::string() : text(row, c);   // its icon alone (its text sorts and searches)
}

std::string JPPackagesTableModel::cellTooltip(int row, int c) const {
    const JPCatalog::Package* e = entry(row);
    if (!e) return {};
    if (c == kStatus) return JPCatalog::statusTip(e->status);
    if (c == kSource) return e->board ? "The open board " + e->board->scopeName() + "'s" : "The library's";
    return {};
}

JPTableModel::Column JPPackagesTableModel::column(int c) const {
    switch (c) {
        case kId:          return { "ID", "", Kind::Text };
        case kSource:      return JPSourceColumn::column();
        case kStatus:      return { "Status", "How it stands to the library", Kind::Text };
        case kDescription: return { "Description", "", Kind::Text };
        case kTape:
            return { "Tape Specification",
                     "This is a text field used by some feeder implementations. Check your feeder documentation for "
                     "more details.",
                     Kind::Text };
        case kBottomVision:   return { "BottomVision", "", Kind::Choice };
        case kFiducialVision: return { "FiducialVision", "", Kind::Choice };
    }
    return {};
}

const JPCatalog::Package* JPPackagesTableModel::entry(int row) const {
    return row >= 0 && size_t(row) < m_rows.size() ? &m_rows[size_t(row)] : nullptr;
}

JPPackage* JPPackagesTableModel::package(int row) const {
    const JPCatalog::Package* e = entry(row);
    return e ? e->package() : nullptr;
}

int JPPackagesTableModel::rowOf(const JPPackage* p) const {
    for (size_t i = 0; i < m_rows.size(); ++i)
        if (m_rows[i].package() == p) return int(i);
    return -1;
}

int JPPackagesTableModel::rowOf(const JPBoard* board, const std::string& packageId) const {
    for (size_t i = 0; i < m_rows.size(); ++i)
        if (m_rows[i].board.get() == board && m_rows[i].held && m_rows[i].held->id == packageId) return int(i);
    return -1;
}

std::string JPPackagesTableModel::rowKey(int row) const {
    const JPCatalog::Package* e = entry(row);
    return e ? JPCatalog::source(e->board) + "|" + e->name + "|" + JPCatalog::statusName(e->status) : std::string();
}

std::string JPPackagesTableModel::text(int row, int c) const {
    const JPCatalog::Package* e = entry(row);
    const JPPackage* p = package(row);
    if (!p) return {};
    switch (c) {
        case kId:          return e->name;
        case kSource:      return JPCatalog::source(e->board);
        case kStatus:      return JPCatalog::statusName(e->status);
        case kDescription: return p->description.value_or("");
        case kTape:        return p->tapeSpecification.value_or("");
        case kBottomVision: {
            const JPVisionSettings* v = m_config.visionSettings(p->bottomVisionId);
            return v ? v->name : std::string();
        }
        case kFiducialVision: {
            const JPVisionSettings* v = m_config.visionSettings(p->fiducialVisionId);
            return v ? v->name : std::string();
        }
    }
    return {};
}

std::vector<const JPVisionSettings*> JPPackagesTableModel::visionChoices(JPVisionSettings::Kind kind) const {
    std::vector<const JPVisionSettings*> out;
    for (const JPVisionSettings& v : m_config.visionSettings())
        if (v.kind == kind) out.push_back(&v);
    std::sort(out.begin(), out.end(), [](const JPVisionSettings* a, const JPVisionSettings* b) { return a->name < b->name; });
    out.push_back(nullptr);
    return out;
}

std::vector<std::string> JPPackagesTableModel::choices(int, int c) const {
    std::vector<std::string> out;
    if (c == kBottomVision || c == kFiducialVision)
        for (const JPVisionSettings* v :
             visionChoices(c == kBottomVision ? JPVisionSettings::Kind::Bottom : JPVisionSettings::Kind::Fiducial))
            out.push_back(v ? v->name : std::string());
    return out;
}

bool JPPackagesTableModel::setText(int row, int c, const std::string& text, std::string&) {
    JPPackage* p = package(row);
    if (!p || !editable(row, c)) return false;
    if (c == kDescription) p->description = text;
    else if (c == kTape) p->tapeSpecification = text;
    else return false;
    if (onChanged) onChanged(entry(row)->board.get());
    return true;
}

void JPPackagesTableModel::setChoice(int row, int c, int index) {
    JPPackage* p = package(row);
    if (!p || !editable(row, c) || (c != kBottomVision && c != kFiducialVision)) return;
    const auto vs = visionChoices(c == kBottomVision ? JPVisionSettings::Kind::Bottom : JPVisionSettings::Kind::Fiducial);
    if (index < 0 || size_t(index) >= vs.size()) return;
    (c == kBottomVision ? p->bottomVisionId : p->fiducialVisionId) = vs[size_t(index)] ? vs[size_t(index)]->id : std::string();
    if (onChanged) onChanged(entry(row)->board.get());
}

} // inline namespace jf
