// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPTableModel.h"

inline namespace jf {

// The Source column of a table of parts or packages (JPCatalog): first, as
// wide as an icon, the library's icon or a board's in each row (its text,
// "Library" or the board's name, sorts and searches and is its tooltip).
class JPSourceColumn {
public:
    static JPTableModel::Column column();
};

} // inline namespace jf
