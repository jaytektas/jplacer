// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "model/JPFieldSet.h"
#include "model/JPFieldValues.h"

#include <j/core/JComboBox.h>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

inline namespace jf {

// What a JPTable shows and edits, as a Swing TableModel: columns with a
// name, a tooltip and a kind (which decides how a cell is drawn, edited and
// sorted), and rows of cells. Rows are the model's; the table sorts and
// filters a view of them without moving them. A model whose rows are things
// with fields (parts, placements, feeders: fieldsOf) gives each column the
// key of the built-in field it shows, and keeps each row's values of the
// fields added (custom, setCustom); the table shows those fields as columns
// of their own and leaves out the built-ins removed (JPFieldsModel).
class JPTableModel {
public:
    enum class Kind {
        Text,      // compared as text, edited as text
        Number,    // compared by number(); drawn on the right
        Boolean,   // a tick box; compared false before true
        Choice,    // one of choices(), picked from a menu; compared as text
        Picker,    // chosen in a window of the model's own (pick()), opened by a click or F2; compared as text
    };
    // Where a cell's text sits; Auto: numbers right, the rest left.
    enum class Align { Auto, Left, Center, Right };
    struct Column {
        std::string name;
        std::string tooltip;
        Kind        kind = Kind::Text;
        float       width = 0;   // as wide as wanted, in the style's units; 0: shared out
        Align       align = Align::Auto;
        // Numbers lined up on their decimal points, the column centred (as
        // OpenPnP's lengths and rotations in its aligned format).
        bool        decimalAligned = false;
        // Hidden until chosen (a table's Columns…): more about each row, kept out of the way of those who do
        // not want it. Hidden in a table whose kept layout has never had it, and again on Reset Columns.
        bool        hiddenAtFirst = false;
        // What it is for good, whatever its heading says (the built-in field it shows, JPField::key); empty:
        // its name. What a table keeps of its columns is kept by it.
        std::string key;
    };
    // A column's key: its own, else its name.
    static std::string keyOf(const Column& c) { return c.key.empty() ? c.name : c.key; }

    virtual ~JPTableModel() = default;

    virtual int    columnCount() const = 0;
    virtual Column column(int c) const = 0;
    virtual int    rowCount() const = 0;
    // What the cell shows (and, for Text and Choice, what is edited and sorted).
    virtual std::string text(int row, int c) const = 0;
    // A Number cell's value; a Boolean cell's state.
    virtual double number(int, int) const { return 0; }
    virtual bool   checked(int, int) const { return false; }
    // Something that names a row whatever its place, so the selection
    // survives the rows being made again.
    virtual std::string rowKey(int row) const { return std::to_string(row); }
    // Whether a row is shown at all (before any search): false hides it.
    virtual bool rowShown(int) const { return true; }

    // How two rows order by a column, when not as its kind orders them
    // (<0, 0, >0); none: by its kind.
    virtual std::optional<int> compare(int /*rowA*/, int /*rowB*/, int /*c*/) const { return std::nullopt; }
    // A cell's tooltip (none: none).
    virtual std::string cellTooltip(int, int) const { return {}; }
    // What a cell shows, when not its text (which is what is sorted,
    // searched and copied): the job table's ids, indented by their depth.
    virtual std::string displayText(int row, int c) const { return text(row, c); }
    // An OpenPnP icon (its name) before a cell's text, or none.
    virtual std::string cellIcon(int, int) const { return {}; }
    // A cell's fill, to stand out from its row (one of the style's colours,
    // as OpenPnP colours a fiducial's type or a placement's status); none:
    // the row's.
    virtual const uint8_t* cellTint(int, int) const { return nullptr; }
    // A cell's text colour (one of the style's colours, as OpenPnP's log
    // colours each level); none: the table's.
    virtual const uint8_t* cellInk(int, int) const { return nullptr; }
    // A cell drawn greyed while its row is not chosen (as a Swing renderer
    // set disabled: the Feeders tab's Enabled for a part the job does not use).
    virtual bool cellDimmed(int, int) const { return false; }

    virtual bool editable(int, int) const { return false; }
    // Rows that can be dragged to another place (OpenPnP's Reorderable):
    // `from` moved to before row `to` (the row count: to the end).
    virtual bool reorderable() const { return false; }
    virtual void reorder(int /*from*/, int /*to*/) {}
    virtual std::vector<std::string> choices(int, int) const { return {}; }
    // A long list of choices searched as one is chosen (a library's parts): how (JComboBox::Search); none, a plain list.
    virtual std::optional<JComboBox::Search> choiceSearch(int, int) const { return std::nullopt; }
    // An edit: false (and why, to be shown) when the value is refused.
    virtual bool setText(int, int, const std::string&, std::string& /*error*/) { return false; }
    virtual void setChoice(int, int, int /*index*/) {}
    // A Picker cell clicked (or F2): its window opened; what it chooses, the model sets itself.
    virtual void pick(int, int) {}
    virtual void setChecked(int, int, bool) {}

    // The kind of thing its rows are, whose fields it shows; none: rows of jplacer's own (a log's lines, the
    // issues found), with no fields to add.
    virtual std::optional<JPFieldSet::Of> fieldsOf() const { return std::nullopt; }
    // A row's values of the fields added; null: the row keeps none (a panel's pseudo-placement).
    virtual const JPFieldValues* custom(int) const { return nullptr; }
    // Whether a row's values of the fields added may be changed here (a job's use of a board edited alone
    // keeps none of its own: they are the board's).
    virtual bool customEditable(int row) const { return custom(row) != nullptr; }
    // One of a row's values set (null: taken out), as an edit of the row is made (saved, its uses told).
    // False when the row keeps none.
    virtual bool setCustom(int, const std::string& /*key*/, const JJson& /*value*/) { return false; }

    // Edits to many rows at once (a value typed into all the rows chosen, a paste): begun, then ended, so the
    // model says the change once (saved and redrawn once, not once a row).
    virtual void beginEdits() {}
    virtual void endEdits() {}
};

} // inline namespace jf
