// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPXmlNode.h"

#include <string>

inline namespace jf {

// Writes a JPXmlNode tree as OpenPnP's files are written: three spaces an
// indent, an element with no text or children closed in place, a newline at
// the end; written whole or not at all (JPWholeFile).
class JPXmlWriter {
public:
    static std::string text(const JPXmlNode& root);
    // `finalNewline`: as OpenPnP ends parts.xml and packages.xml; its
    // boards, panels and jobs end at the root's closing tag.
    static bool write(const std::string& path, const JPXmlNode& root, std::string& error, bool finalNewline = true);
    // A number as Java prints a double ("1.0", "0.25", "1.0E-4"), which
    // OpenPnP's files hold and its readers expect.
    static std::string number(double v);
};

} // inline namespace jf
