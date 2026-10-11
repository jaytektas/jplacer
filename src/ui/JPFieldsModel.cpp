// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPFieldsModel.h"

#include "JPLengthCell.h"

#include "model/JPSystemUnits.h"

inline namespace jf {

namespace {

// A choice's list ends with none, so a value chosen can be taken back out.
constexpr const char* kNoChoice = "";

} // namespace

JPFieldsModel::JPFieldsModel(JPTableModel& source, JPFields& fields, JPFieldSet::Of of)
    : m_source(source), m_fields(fields), m_of(of) {}

const std::vector<JPFieldsModel::Entry>& JPFieldsModel::entries() const {
    const int n = m_source.columnCount();
    if (m_version == m_fields.version() && m_sourceColumns == n) return m_entries;
    m_version = m_fields.version();
    m_sourceColumns = n;
    m_entries.clear();
    const JPFieldSet& s = set();
    for (int c = 0; c < n; ++c) {
        const std::string key = keyOf(m_source.column(c));
        const JPField* f = s.field(key);
        if (f && f->removed) continue;   // a built-in removed: in no table
        m_entries.push_back({ c, key });
    }
    for (const JPField* f : s.added()) m_entries.push_back({ -1, f->key });
    return m_entries;
}

int JPFieldsModel::columnCount() const { return int(entries().size()); }

int JPFieldsModel::sourceColumn(int c) const {
    const auto& e = entries();
    return c >= 0 && size_t(c) < e.size() ? e[size_t(c)].source : -1;
}

const JPField* JPFieldsModel::fieldAt(int c) const {
    const auto& e = entries();
    return c >= 0 && size_t(c) < e.size() ? set().field(e[size_t(c)].key) : nullptr;
}

const JPField* JPFieldsModel::addedAt(int c) const {
    const auto& e = entries();
    if (c < 0 || size_t(c) >= e.size() || e[size_t(c)].source >= 0) return nullptr;
    return set().field(e[size_t(c)].key);
}

const JJson& JPFieldsModel::valueAt(int row, const JPField& f) const {
    static const JJson none;
    const JPFieldValues* v = m_source.custom(row);
    return v ? v->get(f.key) : none;
}

JPTableModel::Column JPFieldsModel::column(int c) const {
    if (const JPField* f = addedAt(c)) {
        Column col { f->name, f->name + ": a field added to " + JPFieldSet::plural(m_of) + " (" + JPField::typeLabel(f->type)
                                  + "; Edit Fields… changes it)",
                     Kind::Text };
        col.key = f->key;
        col.hiddenAtFirst = true;   // shown in the table it was added from, chosen in the others
        switch (f->type) {
            case JPField::Type::Text: col.align = Align::Left; break;
            case JPField::Type::Number: col.kind = Kind::Number; break;
            case JPField::Type::Length: col.decimalAligned = true; break;
            case JPField::Type::YesNo: col.kind = Kind::Boolean; break;
            case JPField::Type::Choice: col.kind = Kind::Choice; col.align = Align::Left; break;
        }
        return col;
    }
    const int s = sourceColumn(c);
    if (s < 0) return {};
    Column col = m_source.column(s);
    col.key = keyOf(col);
    // A built-in renamed: its name, wherever it is shown.
    if (const JPField* f = set().field(col.key); f && set().renamed(f->key)) col.name = f->name;
    return col;
}

std::string JPFieldsModel::text(int row, int c) const {
    if (const JPField* f = addedAt(c)) {
        const JJson& v = valueAt(row, *f);
        if (f->type == JPField::Type::Length && v.isNumber())
            return JPLengthCell::text(JPLength(v.number(), JPLengthUnit::Millimeters), false);
        return f->text(v);
    }
    const int s = sourceColumn(c);
    return s >= 0 ? m_source.text(row, s) : std::string();
}

double JPFieldsModel::number(int row, int c) const {
    if (const JPField* f = addedAt(c)) return valueAt(row, *f).number();
    const int s = sourceColumn(c);
    return s >= 0 ? m_source.number(row, s) : 0;
}

bool JPFieldsModel::checked(int row, int c) const {
    if (const JPField* f = addedAt(c)) return valueAt(row, *f).boolean(false);
    const int s = sourceColumn(c);
    return s >= 0 && m_source.checked(row, s);
}

std::optional<int> JPFieldsModel::compare(int a, int b, int c) const {
    if (const JPField* f = addedAt(c)) {
        // A length by its length; one without a value after those with one.
        if (f->type != JPField::Type::Length) return std::nullopt;
        const JJson &x = valueAt(a, *f), &y = valueAt(b, *f);
        if (x.isNull() != y.isNull()) return x.isNull() ? 1 : -1;
        return x.number() < y.number() ? -1 : x.number() > y.number() ? 1 : 0;
    }
    const int s = sourceColumn(c);
    return s >= 0 ? m_source.compare(a, b, s) : std::nullopt;
}

std::string JPFieldsModel::cellTooltip(int row, int c) const {
    const int s = sourceColumn(c);
    return s >= 0 ? m_source.cellTooltip(row, s) : std::string();
}

std::string JPFieldsModel::displayText(int row, int c) const {
    const int s = sourceColumn(c);
    return s >= 0 ? m_source.displayText(row, s) : text(row, c);
}

std::string JPFieldsModel::cellIcon(int row, int c) const {
    const int s = sourceColumn(c);
    return s >= 0 ? m_source.cellIcon(row, s) : std::string();
}

const uint8_t* JPFieldsModel::cellTint(int row, int c) const {
    const int s = sourceColumn(c);
    return s >= 0 ? m_source.cellTint(row, s) : nullptr;
}

const uint8_t* JPFieldsModel::cellInk(int row, int c) const {
    const int s = sourceColumn(c);
    return s >= 0 ? m_source.cellInk(row, s) : nullptr;
}

bool JPFieldsModel::cellDimmed(int row, int c) const {
    const int s = sourceColumn(c);
    return s >= 0 && m_source.cellDimmed(row, s);
}

bool JPFieldsModel::editable(int row, int c) const {
    if (addedAt(c)) return m_source.customEditable(row);
    const int s = sourceColumn(c);
    return s >= 0 && m_source.editable(row, s);
}

std::vector<std::string> JPFieldsModel::choices(int row, int c) const {
    if (const JPField* f = addedAt(c)) {
        std::vector<std::string> out = f->choices;
        out.push_back(kNoChoice);
        return out;
    }
    const int s = sourceColumn(c);
    return s >= 0 ? m_source.choices(row, s) : std::vector<std::string>();
}

std::optional<JComboBox::Search> JPFieldsModel::choiceSearch(int row, int c) const {
    const int s = sourceColumn(c);
    return s >= 0 ? m_source.choiceSearch(row, s) : std::nullopt;
}

bool JPFieldsModel::setText(int row, int c, const std::string& text, std::string& error) {
    if (const JPField* f = addedAt(c)) {
        JJson v;
        if (!f->parse(text, v, error, JPSystemUnits::units())) return false;
        if (!m_source.setCustom(row, f->key, v)) {
            error = "This row keeps no values of the fields added";
            return false;
        }
        return true;
    }
    const int s = sourceColumn(c);
    return s >= 0 && m_source.setText(row, s, text, error);
}

void JPFieldsModel::setChoice(int row, int c, int index) {
    if (const JPField* f = addedAt(c)) {
        if (index >= 0 && size_t(index) < f->choices.size()) m_source.setCustom(row, f->key, JJson(f->choices[size_t(index)]));
        else if (size_t(index) == f->choices.size()) m_source.setCustom(row, f->key, JJson());
        return;
    }
    const int s = sourceColumn(c);
    if (s >= 0) m_source.setChoice(row, s, index);
}

void JPFieldsModel::pick(int row, int c) {
    const int s = sourceColumn(c);
    if (s >= 0) m_source.pick(row, s);
}

void JPFieldsModel::setChecked(int row, int c, bool on) {
    if (const JPField* f = addedAt(c)) {
        m_source.setCustom(row, f->key, on ? JJson(true) : JJson());
        return;
    }
    const int s = sourceColumn(c);
    if (s >= 0) m_source.setChecked(row, s, on);
}

} // inline namespace jf
