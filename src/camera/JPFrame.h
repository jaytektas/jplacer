// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include <cstdint>
#include <vector>

inline namespace jf {

// One picture from a camera, as RGBA (8 bits a channel, rows top to bottom).
// `sequence` counts frames from the camera's start, so a viewer can tell a
// new frame from the one it already shows.
struct JPFrame {
    int                  width  = 0;
    int                  height = 0;
    std::vector<uint8_t> rgba;
    uint64_t             sequence = 0;
};

} // inline namespace jf
