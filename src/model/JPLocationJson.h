// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPLocation.h"

#include <j/config/Json.h>

inline namespace jf {

// A location in jplacer's JSON files: {"units", "x", "y", "z", "rotation"}.
class JPLocationJson {
public:
    static JJson      to(const JPLocation& l);
    static JPLocation from(const JJson& j);
};

} // inline namespace jf
