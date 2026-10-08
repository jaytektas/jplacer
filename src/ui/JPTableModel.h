// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include <optional>
#include <string>
#include <vector>

inline namespace jf {

// What a JPTable shows and edits, as a Swing TableModel: columns with a
// name, a tooltip and a kind (which decides how a cell is drawn, edited and
// sorted), and rows of cells. Rows are the model's; the table sorts and
// filters a view of them without moving them.
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
    };

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
    // An edit: false (and why, to be shown) when the value is refused.
    virtual bool setText(int, int, const std::string&, std::string& /*error*/) { return false; }
    virtual void setChoice(int, int, int /*index*/) {}
    // A Picker cell clicked (or F2): its window opened; what it chooses, the model sets itself.
    virtual void pick(int, int) {}
    virtual void setChecked(int, int, bool) {}
};

} // inline namespace jf
