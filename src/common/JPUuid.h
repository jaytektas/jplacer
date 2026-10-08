// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include <string>

inline namespace jf {

// A random (version 4) UUID, as text: what a library part or package is known by for good, whatever it is
// named or renamed to (DESIGN.md, Storage).
class JPUuid {
public:
    static std::string make();
};

} // inline namespace jf
