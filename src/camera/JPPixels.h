// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

inline namespace jf {

// Turning what a camera sends into RGBA.
class JPPixels {
public:
    // YUYV 4:2:2 (two pixels in four bytes) to RGBA.
    static void yuyvToRgba(const uint8_t* yuyv, int width, int height, std::vector<uint8_t>& rgba);

    // A JPEG (MJPG frames are one each) to RGBA. False with `error` when the
    // data is not a JPEG it can read.
    static bool jpegToRgba(const uint8_t* data, size_t size, int& width, int& height,
                           std::vector<uint8_t>& rgba, std::string& error);
};

} // inline namespace jf
