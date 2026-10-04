// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPXmlElement.h"

#include <string>

inline namespace jf {

// Reads an XML file into a JPXmlElement tree (expat underneath).
class JPXmlReader {
public:
    // False with `error` (file, line and expat's reason) when the file cannot
    // be read or is not well-formed.
    static bool read(const std::string& path, JPXmlElement& root, std::string& error);
    // The same from text (the clipboard's, say); `error` gives the line.
    static bool parse(const std::string& text, JPXmlElement& root, std::string& error);
};

} // inline namespace jf
