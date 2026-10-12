// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPTableModel.h"

#include "model/JPFields.h"

#include <string>
#include <vector>

inline namespace jf {

// A table model with its kind's fields (JPFieldSet): the source model's columns but the built-ins removed (a
// column whose key names no field of the kind is kept), each built-in by its own name where it was renamed,
// then a column for each field added, its cells the rows' values (JPTableModel::custom) shown and edited as
// its type says: text and numbers as typed, a length in the System Units, Yes/No a tick, a choice from its
// list. Everything else is the source's. Made again as the fields change (JPFields::version).
class JPFieldsModel : public JPTableModel {
public:
    JPFieldsModel(JPTableModel& source, JPFields& fields, JPFieldSet::Of of);

    JPTableModel&     source() const { return m_source; }
    const JPFieldSet& set() const { return m_fields.of(m_of); }
    // The field a column shows (a built-in's, one added's), or null (a column of the source's no field names).
    const JPField* fieldAt(int c) const;
    // The source's column a column is, or -1 (a field added's).
    int sourceColumn(int c) const;

    int    columnCount() const override;
    Column column(int c) const override;
    int    rowCount() const override { return m_source.rowCount(); }
    std::string text(int row, int c) const override;
    double number(int row, int c) const override;
    bool   checked(int row, int c) const override;
    std::string rowKey(int row) const override { return m_source.rowKey(row); }
    bool   rowShown(int row) const override { return m_source.rowShown(row); }
    std::optional<int> compare(int a, int b, int c) const override;
    std::string cellTooltip(int row, int c) const override;
    std::string displayText(int row, int c) const override;
    std::string cellIcon(int row, int c) const override;
    const uint8_t* cellTint(int row, int c) const override;
    const uint8_t* cellInk(int row, int c) const override;
    bool   cellDimmed(int row, int c) const override;
    bool   editable(int row, int c) const override;
    bool   reorderable() const override { return m_source.reorderable(); }
    void   reorder(int from, int to) override { m_source.reorder(from, to); }
    std::vector<std::string> choices(int row, int c) const override;
    std::optional<JComboBox::Search> choiceSearch(int row, int c) const override;
    bool   setText(int row, int c, const std::string& text, std::string& error) override;
    void   setChoice(int row, int c, int index) override;
    void   pick(int row, int c) override;
    void   setChecked(int row, int c, bool on) override;
    std::optional<JPFieldSet::Of> fieldsOf() const override { return m_of; }
    const JPFieldValues* custom(int row) const override { return m_source.custom(row); }
    bool   customEditable(int row) const override { return m_source.customEditable(row); }
    bool   setCustom(int row, const std::string& key, const JJson& value) override { return m_source.setCustom(row, key, value); }
    bool   ownHistory() const override { return m_source.ownHistory(); }
    void   beginEdits() override { m_source.beginEdits(); }
    void   endEdits() override { m_source.endEdits(); }

private:
    struct Entry {
        int         source = -1;   // the source's column; -1: a field added
        std::string key;           // the field's (or the source column's) key
    };
    const std::vector<Entry>& entries() const;
    // The field added a column shows, or null.
    const JPField* addedAt(int c) const;
    const JJson&   valueAt(int row, const JPField& f) const;

    JPTableModel&              m_source;
    JPFields&                  m_fields;
    JPFieldSet::Of             m_of;
    mutable std::vector<Entry> m_entries;
    mutable unsigned           m_version = 0;
    mutable int                m_sourceColumns = -1;
};

} // inline namespace jf
