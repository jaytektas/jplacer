// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPImageTransform.h"

#include <cstring>
#include <vector>

inline namespace jf {

namespace {
constexpr size_t kBytesPerPixel = 4;   // RGBA
}

void JPImageTransform::deinterlace(JPFrame& frame) {
    const size_t row = size_t(frame.width) * kBytesPerPixel;
    const int half = frame.height / 2;
    if (half <= 0 || frame.rgba.size() < row * size_t(frame.height)) return;
    std::vector<uint8_t> woven(frame.rgba.size());
    // As OpenPnP's: the top half's rows to the even lines, the bottom half's to the odd ones.
    for (int i = 0; i < half; ++i) {
        std::memcpy(&woven[size_t(2 * i) * row], &frame.rgba[size_t(i) * row], row);
        std::memcpy(&woven[size_t(2 * i + 1) * row], &frame.rgba[size_t(i + half) * row], row);
    }
    // An odd last line has no partner: kept where it is.
    if (frame.height % 2)
        std::memcpy(&woven[size_t(frame.height - 1) * row], &frame.rgba[size_t(frame.height - 1) * row], row);
    frame.rgba = std::move(woven);
}

void JPImageTransform::crop(JPFrame& frame, int width, int height) {
    const int w = width > 0 && width < frame.width ? width : frame.width;
    const int h = height > 0 && height < frame.height ? height : frame.height;
    if (w == frame.width && h == frame.height) return;
    const int x0 = frame.width / 2 - w / 2, y0 = frame.height / 2 - h / 2;
    std::vector<uint8_t> cut(size_t(w) * size_t(h) * kBytesPerPixel);
    for (int y = 0; y < h; ++y)
        std::memcpy(&cut[size_t(y) * size_t(w) * kBytesPerPixel],
                    &frame.rgba[(size_t(y0 + y) * size_t(frame.width) + size_t(x0)) * kBytesPerPixel], size_t(w) * kBytesPerPixel);
    frame.rgba = std::move(cut);
    frame.width = w;
    frame.height = h;
}

} // inline namespace jf
