// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include <string>
#include <vector>

inline namespace jf {

// The designators a BOM line names: "R1, R2, R5-R8", "C3 C4", "R5-8", "U1;U2"
// (commas, semicolons or spaces between them; a range of one prefix,
// its end with the prefix or without), each once, in order.
class JPDesignators {
public:
    static std::vector<std::string> expand(const std::string& text);
};

} // inline namespace jf
