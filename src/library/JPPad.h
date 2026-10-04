// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include <j/config/Json.h>

#include <string>

inline namespace jf {

// One pad of a footprint, in the footprint's own millimetres (its centre at
// 0, 0, at the package's 0°): where its middle is, its size, how far it is
// turned, and how round its corners are (0 square, 1 a full round: a circle
// or an obround).
struct JPPad {
    std::string name;            // "1", "A1", "EP"
    double      x = 0, y = 0;
    double      width = 0, height = 0;
    double      rotationDeg = 0;
    double      roundness = 0;

    static JPPad fromJson(const JJson& j);
    JJson toJson() const;
    bool operator==(const JPPad&) const = default;
};

} // inline namespace jf
