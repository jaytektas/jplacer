// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPIconButton.h"

#include <string>
#include <vector>

inline namespace jf {

// The icons a table's view is shown by on its button (JPTable's views), each drawn as JPIcons draws its
// own: by a name kept with the view ("star"), and what it is called where one is chosen ("Star").
class JPViewIcons {
public:
    struct Icon {
        const char* key;
        const char* label;
    };
    // Every one, in the order offered: list, columns, star, truck, wrench, eye, person, clipboard, tag, flag,
    // box, gear, check.
    static const std::vector<Icon>& all();
    // The glyph of `key` (one not known: the list's).
    static JPIconButton::Glyph glyph(const std::string& key);
    static std::string label(const std::string& key);
};

} // inline namespace jf
