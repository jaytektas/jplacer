// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPPlacementsHolderTableModel.h"

#include "JPLengthCell.h"

#include "model/JPDefinitionChanges.h"

inline namespace jf {

namespace {
enum Col { kName, kWidth, kLength };
}

JPPlacementsHolderTableModel::JPPlacementsHolderTableModel(JPConfiguration& config, JPPlacementsHolder::Kind kind,
                                                           std::function<const JPJob*()> job)
    : m_config(config), m_kind(kind), m_job(std::move(job)) {}

JPTableModel::Column JPPlacementsHolderTableModel::column(int c) const {
    Column col;
    switch (c) {
        case kName:
            col.name = m_kind == JPPlacementsHolder::Kind::Board ? "Board Name" : "Panel Name";
            break;
        case kWidth:
        case kLength:
            col.name = c == kWidth ? "Width" : "Length";
            col.align = Align::Center;
            col.decimalAligned = true;
            break;
    }
    return col;
}

int JPPlacementsHolderTableModel::rowCount() const {
    return int(m_kind == JPPlacementsHolder::Kind::Board ? m_config.boards().size() : m_config.panels().size());
}

JPPlacementsHolder* JPPlacementsHolderTableModel::holder(int row) const {
    if (row < 0 || row >= rowCount()) return nullptr;
    if (m_kind == JPPlacementsHolder::Kind::Board) return m_config.boards()[size_t(row)].get();
    return m_config.panels()[size_t(row)].get();
}

int JPPlacementsHolderTableModel::rowOf(const JPPlacementsHolder* h) const {
    for (int r = 0; r < rowCount(); ++r)
        if (holder(r) == h) return r;
    return -1;
}

std::string JPPlacementsHolderTableModel::rowKey(int row) const {
    const JPPlacementsHolder* h = holder(row);
    return h ? h->file : std::string();
}

std::string JPPlacementsHolderTableModel::text(int row, int c) const {
    const JPPlacementsHolder* h = holder(row);
    if (!h) return {};
    switch (c) {
        case kName:   return h->name.value_or("");
        case kWidth:  return JPLengthCell::text(h->dimensions.lengthX(), true);
        case kLength: return JPLengthCell::text(h->dimensions.lengthY(), true);
    }
    return {};
}

std::optional<int> JPPlacementsHolderTableModel::compare(int a, int b, int c) const {
    if (c == kName) return std::nullopt;
    // As OpenPnP's LengthCellValue: by the numbers as kept, whatever their units.
    const JPPlacementsHolder *x = holder(a), *y = holder(b);
    const double u = c == kWidth ? x->dimensions.x() : x->dimensions.y();
    const double v = c == kWidth ? y->dimensions.x() : y->dimensions.y();
    return u < v ? -1 : u > v ? 1 : 0;
}

std::string JPPlacementsHolderTableModel::cellTooltip(int row, int c) const {
    const JPPlacementsHolder* h = holder(row);
    return h && c == kName ? h->file : std::string();
}

bool JPPlacementsHolderTableModel::setText(int row, int c, const std::string& text, std::string&) {
    JPPlacementsHolder* h = holder(row);
    if (!h) return false;
    JPDefinitionChanges changes(m_config, m_job());
    if (c == kName) {
        changes.holder(*h, [&text](JPPlacementsHolder& x) { x.name = text; });
    } else {
        const auto length = JPLength::parse(text);
        if (!length) return false;
        const JPLocation::Field f = c == kWidth ? JPLocation::Field::X : JPLocation::Field::Y;
        const JPLocation dims = h->dimensions.withField(f, *length, false);
        changes.holder(*h, [&dims](JPPlacementsHolder& x) { x.setDimensions(dims); });
    }
    if (onChanged) onChanged();
    return true;
}

} // inline namespace jf
