// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPLinkedColumnsModel.h"

#include <algorithm>

inline namespace jf {

JPLinkedColumnsModel::JPLinkedColumnsModel(JPTableModel& own, std::vector<Link> links)
    : m_own(own), m_links(std::move(links)), m_edited(m_links.size()) {}

const std::vector<JPLinkedColumnsModel::Place>& JPLinkedColumnsModel::places() const {
    // Made again when a model's columns change (a field added to parts).
    std::vector<int> counts { m_own.columnCount() };
    for (const Link& l : m_links) counts.push_back(l.model->columnCount());
    if (counts == m_counts) return m_places;
    m_counts = counts;
    m_places.clear();
    for (int c = 0; c < counts[0]; ++c) m_places.push_back({ -1, c });
    for (size_t i = 0; i < m_links.size(); ++i) {
        const Link& l = m_links[i];
        for (int c = 0; c < l.model->columnCount(); ++c) {
            const std::string key = keyOf(l.model->column(c));
            if (std::find(l.leftOut.begin(), l.leftOut.end(), key) == l.leftOut.end()) m_places.push_back({ int(i), c });
        }
    }
    return m_places;
}

JPLinkedColumnsModel::Place JPLinkedColumnsModel::place(int c) const {
    const auto& p = places();
    return c >= 0 && size_t(c) < p.size() ? p[size_t(c)] : Place {};
}

int JPLinkedColumnsModel::linkedRow(const Place& p, int row) const {
    if (p.link < 0) return -1;
    const Link& l = m_links[size_t(p.link)];
    const int r = l.rowOf ? l.rowOf(row) : -1;
    return r >= 0 && r < l.model->rowCount() ? r : -1;
}

int JPLinkedColumnsModel::columnCount() const { return int(places().size()); }

JPTableModel::Column JPLinkedColumnsModel::column(int c) const {
    const Place p = place(c);
    if (p.link < 0) return m_own.column(p.column);
    const Link& l = m_links[size_t(p.link)];
    Column col = l.model->column(p.column);
    const std::string key = keyOf(col);
    std::string what = col.name;
    // A column without a title (an icon's) by what its tooltip says it is, before its colon.
    if (what.empty()) what = col.tooltip.substr(0, col.tooltip.find(':'));
    col.name = l.name + " \xE2\x96\xB8 " + what;
    col.tooltip = "The " + l.name + "'s, changed for it wherever it is used" + (col.tooltip.empty() ? "" : ": " + col.tooltip);
    col.key = l.key + "." + key;
    col.hiddenAtFirst = true;
    col.width = 0;
    return col;
}

std::string JPLinkedColumnsModel::text(int row, int c) const {
    const Place p = place(c);
    if (p.link < 0) return m_own.text(row, p.column);
    const int r = linkedRow(p, row);
    return r < 0 ? std::string() : m_links[size_t(p.link)].model->text(r, p.column);
}

double JPLinkedColumnsModel::number(int row, int c) const {
    const Place p = place(c);
    if (p.link < 0) return m_own.number(row, p.column);
    const int r = linkedRow(p, row);
    return r < 0 ? 0 : m_links[size_t(p.link)].model->number(r, p.column);
}

bool JPLinkedColumnsModel::checked(int row, int c) const {
    const Place p = place(c);
    if (p.link < 0) return m_own.checked(row, p.column);
    const int r = linkedRow(p, row);
    return r >= 0 && m_links[size_t(p.link)].model->checked(r, p.column);
}

std::optional<int> JPLinkedColumnsModel::compare(int a, int b, int c) const {
    const Place p = place(c);
    if (p.link < 0) return m_own.compare(a, b, p.column);
    const int ra = linkedRow(p, a), rb = linkedRow(p, b);
    if (ra < 0 || rb < 0) return ra < 0 && rb < 0 ? 0 : (ra < 0 ? -1 : 1);   // those linked to nothing first
    return m_links[size_t(p.link)].model->compare(ra, rb, p.column);
}

std::string JPLinkedColumnsModel::cellTooltip(int row, int c) const {
    const Place p = place(c);
    if (p.link < 0) return m_own.cellTooltip(row, p.column);
    const int r = linkedRow(p, row);
    return r < 0 ? "No " + m_links[size_t(p.link)].name + ": nothing to show or change"
                 : m_links[size_t(p.link)].model->cellTooltip(r, p.column);
}

std::string JPLinkedColumnsModel::displayText(int row, int c) const {
    const Place p = place(c);
    if (p.link < 0) return m_own.displayText(row, p.column);
    const int r = linkedRow(p, row);
    return r < 0 ? std::string() : m_links[size_t(p.link)].model->displayText(r, p.column);
}

std::string JPLinkedColumnsModel::cellIcon(int row, int c) const {
    const Place p = place(c);
    if (p.link < 0) return m_own.cellIcon(row, p.column);
    const int r = linkedRow(p, row);
    return r < 0 ? std::string() : m_links[size_t(p.link)].model->cellIcon(r, p.column);
}

const uint8_t* JPLinkedColumnsModel::cellTint(int row, int c) const {
    const Place p = place(c);
    if (p.link < 0) return m_own.cellTint(row, p.column);
    const int r = linkedRow(p, row);
    return r < 0 ? nullptr : m_links[size_t(p.link)].model->cellTint(r, p.column);
}

const uint8_t* JPLinkedColumnsModel::cellInk(int row, int c) const {
    const Place p = place(c);
    if (p.link < 0) return m_own.cellInk(row, p.column);
    const int r = linkedRow(p, row);
    return r < 0 ? nullptr : m_links[size_t(p.link)].model->cellInk(r, p.column);
}

bool JPLinkedColumnsModel::cellDimmed(int row, int c) const {
    const Place p = place(c);
    if (p.link < 0) return m_own.cellDimmed(row, p.column);
    const int r = linkedRow(p, row);
    return r < 0 || m_links[size_t(p.link)].model->cellDimmed(r, p.column);
}

bool JPLinkedColumnsModel::editable(int row, int c) const {
    const Place p = place(c);
    if (p.link < 0) return m_own.editable(row, p.column);
    const int r = linkedRow(p, row);
    return r >= 0 && m_links[size_t(p.link)].model->editable(r, p.column);
}

std::vector<std::string> JPLinkedColumnsModel::choices(int row, int c) const {
    const Place p = place(c);
    if (p.link < 0) return m_own.choices(row, p.column);
    const int r = linkedRow(p, row);
    return r < 0 ? std::vector<std::string>() : m_links[size_t(p.link)].model->choices(r, p.column);
}

std::optional<JComboBox::Search> JPLinkedColumnsModel::choiceSearch(int row, int c) const {
    const Place p = place(c);
    if (p.link < 0) return m_own.choiceSearch(row, p.column);
    const int r = linkedRow(p, row);
    return r < 0 ? std::nullopt : m_links[size_t(p.link)].model->choiceSearch(r, p.column);
}

bool JPLinkedColumnsModel::setText(int row, int c, const std::string& text, std::string& error) {
    const Place p = place(c);
    if (p.link < 0) return m_own.setText(row, p.column, text, error);
    const int r = linkedRow(p, row);
    if (r < 0) {
        error = "No " + m_links[size_t(p.link)].name + " to change";
        return false;
    }
    if (!m_links[size_t(p.link)].model->setText(r, p.column, text, error)) return false;
    editedRow(p.link, r);
    return true;
}

void JPLinkedColumnsModel::setChoice(int row, int c, int index) {
    const Place p = place(c);
    if (p.link < 0) return m_own.setChoice(row, p.column, index);
    const int r = linkedRow(p, row);
    if (r < 0) return;
    m_links[size_t(p.link)].model->setChoice(r, p.column, index);
    editedRow(p.link, r);
}

void JPLinkedColumnsModel::pick(int row, int c) {
    const Place p = place(c);
    if (p.link < 0) return m_own.pick(row, p.column);
    if (const int r = linkedRow(p, row); r >= 0) m_links[size_t(p.link)].model->pick(r, p.column);
}

void JPLinkedColumnsModel::setChecked(int row, int c, bool on) {
    const Place p = place(c);
    if (p.link < 0) return m_own.setChecked(row, p.column, on);
    const int r = linkedRow(p, row);
    if (r < 0) return;
    m_links[size_t(p.link)].model->setChecked(r, p.column, on);
    editedRow(p.link, r);
}

bool JPLinkedColumnsModel::ownHistoryAt(int c) const {
    const Place p = place(c);
    return p.link < 0 ? m_own.ownHistoryAt(p.column) : m_links[size_t(p.link)].model->ownHistoryAt(p.column);
}

void JPLinkedColumnsModel::beginEdits() {
    ++m_editing;
    m_own.beginEdits();
    for (const Link& l : m_links) l.model->beginEdits();
}

void JPLinkedColumnsModel::endEdits() {
    m_own.endEdits();
    for (const Link& l : m_links) l.model->endEdits();
    if (m_editing > 0 && --m_editing > 0) return;
    for (size_t i = 0; i < m_links.size(); ++i) {
        if (m_edited[i].empty()) continue;
        const std::set<int> rows = std::move(m_edited[i]);
        m_edited[i].clear();
        if (m_links[i].edited) m_links[i].edited(rows);
    }
}

void JPLinkedColumnsModel::editedRow(int link, int row) {
    if (m_editing > 0) {
        m_edited[size_t(link)].insert(row);
        return;
    }
    if (m_links[size_t(link)].edited) m_links[size_t(link)].edited({ row });
}

} // inline namespace jf
