// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPXmlNode.h"

#include <string>

inline namespace jf {

// A JPXmlNode tree as text, as OpenPnP writes its files (what is copied for
// OpenPnP: a pipeline, a part, vision settings): three spaces an indent, an
// element with no text or children closed in place, a newline at the end.
class JPXmlWriter {
public:
    static std::string text(const JPXmlNode& root);
    // A number as Java prints a double ("1.0", "0.25", "1.0E-4"), which
    // OpenPnP's files hold and its readers expect.
    static std::string number(double v);
};

} // inline namespace jf
