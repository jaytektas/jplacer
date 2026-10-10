// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPSourceColumn.h"

#include <j/core/JStyle.h>
#include <j/core/JTextHelper.h>

inline namespace jf {

JPTableModel::Column JPSourceColumn::column() {
    const JStyle& st = JStyle::current();
    JPTableModel::Column c { "", "Source: where it lives, the library or the open board named (its icon)",
                             JPTableModel::Kind::Text };
    c.width = JTextHelper::lineHeight() + 2 * st.gridCellPadding;   // its icon and the cell's padding
    c.align = JPTableModel::Align::Center;
    return c;
}

} // inline namespace jf
