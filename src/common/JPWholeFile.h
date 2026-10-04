// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include <string>

inline namespace jf {

// A file written whole or not at all: the text goes to a new file beside it,
// which takes the old one's place only once it is complete, so a crash or a
// full disk part way leaves the old file as it was. Its folder is made if
// need be.
class JPWholeFile {
public:
    static bool write(const std::string& path, const std::string& text, std::string& error);
};

} // inline namespace jf
