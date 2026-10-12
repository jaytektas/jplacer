// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPTableModel.h"

#include <functional>
#include <set>
#include <string>
#include <vector>

inline namespace jf {

// A table model's rows with the columns of what each row links to (a placement's part, its part's package):
// the model's own columns, then each link's, headed "<link> ▸ <column>" and hidden until chosen (a table's
// Columns…). A link's cell is that row's linked row in the link's model (another table's: the Parts tab's,
// with its fields), shown and edited as that table does it, so an edit changes the thing linked to, for every
// row linked to it; a row linked to nothing shows nothing there and edits nothing. Everything else is the
// model's own.
class JPLinkedColumnsModel : public JPTableModel {
public:
    struct Link {
        std::string                name;   // what the headings say it is ("Part")
        std::string                key;    // its columns' keys' prefix ("part")
        JPTableModel*              model = nullptr;
        std::vector<std::string>   leftOut;   // the keys of its columns not offered (a GUID, an icon)
        std::function<int(int row)> rowOf;   // the model's row a row links to; -1: none
        // Its rows changed by edits here (once for all the rows changed together).
        std::function<void(const std::set<int>& rows)> edited;
    };

    JPLinkedColumnsModel(JPTableModel& own, std::vector<Link> links);

    JPTableModel& own() const { return m_own; }

    int    columnCount() const override;
    Column column(int c) const override;
    int    rowCount() const override { return m_own.rowCount(); }
    std::string text(int row, int c) const override;
    double number(int row, int c) const override;
    bool   checked(int row, int c) const override;
    std::string rowKey(int row) const override { return m_own.rowKey(row); }
    bool   rowShown(int row) const override { return m_own.rowShown(row); }
    std::optional<int> compare(int a, int b, int c) const override;
    std::string cellTooltip(int row, int c) const override;
    std::string displayText(int row, int c) const override;
    std::string cellIcon(int row, int c) const override;
    const uint8_t* cellTint(int row, int c) const override;
    const uint8_t* cellInk(int row, int c) const override;
    bool   cellDimmed(int row, int c) const override;
    bool   editable(int row, int c) const override;
    bool   reorderable() const override { return m_own.reorderable(); }
    void   reorder(int from, int to) override { m_own.reorder(from, to); }
    std::vector<std::string> choices(int row, int c) const override;
    std::optional<JComboBox::Search> choiceSearch(int row, int c) const override;
    bool   setText(int row, int c, const std::string& text, std::string& error) override;
    void   setChoice(int row, int c, int index) override;
    void   pick(int row, int c) override;
    void   setChecked(int row, int c, bool on) override;
    std::optional<JPFieldSet::Of> fieldsOf() const override { return m_own.fieldsOf(); }
    const JPFieldValues* custom(int row) const override { return m_own.custom(row); }
    bool   customEditable(int row) const override { return m_own.customEditable(row); }
    bool   setCustom(int row, const std::string& key, const JJson& value) override { return m_own.setCustom(row, key, value); }
    bool   ownHistory() const override { return m_own.ownHistory(); }
    bool   ownHistoryAt(int c) const override;
    void   beginEdits() override;
    void   endEdits() override;

private:
    // Where a column is: the own model's (link -1) or a link's, by its column there.
    struct Place {
        int link = -1;
        int column = -1;
    };
    const std::vector<Place>& places() const;
    Place place(int c) const;
    // The linked row of `row` for a link's column (-1: none).
    int linkedRow(const Place& p, int row) const;
    // A link's row changed: told now, or at the end of the edits being made.
    void editedRow(int link, int row);

    JPTableModel&                   m_own;
    std::vector<Link>               m_links;
    mutable std::vector<Place>      m_places;
    mutable std::vector<int>        m_counts;   // the column counts the places were made for
    int                             m_editing = 0;
    std::vector<std::set<int>>      m_edited;   // each link's rows changed, while edits are being made
};

} // inline namespace jf
