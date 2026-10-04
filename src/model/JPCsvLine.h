// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include <string>
#include <vector>

inline namespace jf {

// A line of comma (or other) separated values, as the CSV parser OpenPnP
// uses (Ostermiller's) reads one: spaces and tabs around a field dropped; a
// field in double quotes may hold the separator, with a backslash taking
// the next character as it is (\n, \r, \t as those); after the closing
// quote the rest of the field is passed over. A blank line has no fields.
struct JPCsvLine {
    static std::vector<std::string> split(const std::string& line, char separator = ',');
};

} // inline namespace jf
