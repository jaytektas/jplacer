// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

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
    };
    struct Column {
        std::string name;
        std::string tooltip;
        Kind        kind = Kind::Text;
        float       width = 0;   // as wide as wanted, in the style's units; 0: shared out
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

    virtual bool editable(int, int) const { return false; }
    virtual std::vector<std::string> choices(int, int) const { return {}; }
    // An edit: false (and why, to be shown) when the value is refused.
    virtual bool setText(int, int, const std::string&, std::string& /*error*/) { return false; }
    virtual void setChoice(int, int, int /*index*/) {}
    virtual void setChecked(int, int, bool) {}
};

} // inline namespace jf
