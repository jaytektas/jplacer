// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPBatchedChange.h"
#include "JPTableModel.h"

#include "model/JPFootprint.h"

#include <functional>
#include <string>

inline namespace jf {

// A footprint's pads, as OpenPnP's FootprintTableModel: Name, Mark ("O" on
// pin 1), X, Y, Width, Length (lengths in the footprint's units, typed with
// or without units), Rot., % Round (clamped to ±100). All but the mark are
// edited in place.
class JPFootprintTableModel : public JPTableModel {
public:
    // `footprint`: null shows nothing.
    void setFootprint(JPFootprint* footprint) { m_footprint = footprint; }
    std::function<void()> onChanged;

    int    columnCount() const override { return 8; }
    Column column(int c) const override;
    int    rowCount() const override { return m_footprint ? int(m_footprint->pads.size()) : 0; }
    std::string text(int row, int c) const override;
    std::string rowKey(int row) const override { return std::to_string(row); }
    bool   editable(int, int c) const override { return c != 1; }
    bool   setText(int row, int c, const std::string& text, std::string& error) override;

    std::optional<JPFieldSet::Of> fieldsOf() const override { return JPFieldSet::Of::Pads; }
    const JPFieldValues* custom(int row) const override;
    bool   setCustom(int row, const std::string& key, const JJson& value) override;
    bool   ownHistory() const override { return true; }   // the library's (JPLibraryHistory)
    void   beginEdits() override { m_said.begin(); }
    void   endEdits() override { m_said.end(onChanged); }

private:
    Column heading(int c) const;
    // The field a column shows (JPFieldSet's built-in's key).
    std::string keyAt(int c) const;
    JPBatchedChange m_said;
    JPFootprint* m_footprint = nullptr;
};

} // inline namespace jf
