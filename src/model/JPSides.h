// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPSide.h"

#include <string>

inline namespace jf {

// Names for a JPSide ("Top", "Bottom", as OpenPnP's files hold them), and
// the other side.
struct JPSides {
    static const char* name(JPSide s) { return s == JPSide::Bottom ? "Bottom" : "Top"; }
    static JPSide fromName(const std::string& s) { return s == "Bottom" ? JPSide::Bottom : JPSide::Top; }
    static JPSide flip(JPSide s) { return s == JPSide::Top ? JPSide::Bottom : JPSide::Top; }
    static JPSide flip(JPSide s, bool doIt) { return doIt ? flip(s) : s; }
};

} // inline namespace jf
