// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPImageTransform.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

inline namespace jf {

namespace {
constexpr size_t kBytesPerPixel = 4;   // RGBA
constexpr double kPi = 3.14159265358979323846;

// The picture's colour at (x, y), between its pixels' (bilinear); black outside it.
void sample(const JPFrame& f, double x, double y, uint8_t* out) {
    const int x0 = int(std::floor(x)), y0 = int(std::floor(y));
    const double fx = x - x0, fy = y - y0;
    double acc[4] = { 0, 0, 0, 0 };
    for (int dy = 0; dy <= 1; ++dy)
        for (int dx = 0; dx <= 1; ++dx) {
            const int px = x0 + dx, py = y0 + dy;
            if (px < 0 || py < 0 || px >= f.width || py >= f.height) continue;
            const double w = (dx ? fx : 1 - fx) * (dy ? fy : 1 - fy);
            const uint8_t* p = &f.rgba[(size_t(py) * size_t(f.width) + size_t(px)) * kBytesPerPixel];
            for (int c = 0; c < 3; ++c) acc[c] += w * p[c];
        }
    for (int c = 0; c < 3; ++c) out[c] = uint8_t(std::clamp(std::lround(acc[c]), 0L, 255L));
    out[3] = 255;
}

bool whole(const JPFrame& f) {
    return f.width > 0 && f.height > 0 && f.rgba.size() >= size_t(f.width) * size_t(f.height) * kBytesPerPixel;
}
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

void JPImageTransform::scale(JPFrame& frame, int width, int height) {
    if (!whole(frame) || (width <= 0 && height <= 0)) return;
    const int w = width > 0 ? width : frame.width, h = height > 0 ? height : frame.height;
    if (w == frame.width && h == frame.height) return;
    JPFrame out = frame;
    out.width = w;
    out.height = h;
    out.rgba.assign(size_t(w) * size_t(h) * kBytesPerPixel, 0);
    const double sx = double(frame.width) / w, sy = double(frame.height) / h;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            sample(frame, (x + 0.5) * sx - 0.5, (y + 0.5) * sy - 0.5, &out.rgba[(size_t(y) * size_t(w) + size_t(x)) * kBytesPerPixel]);
    frame = std::move(out);
}

void JPImageTransform::rotate(JPFrame& frame, double degrees) {
    if (!whole(frame) || degrees == 0) return;
    // As OpenCV's getRotationMatrix2D with the picture grown to the turned bounds (OpenPnP's rotate):
    // counter-clockwise as seen, the picture's y running down.
    const double a = degrees * kPi / 180, c = std::cos(a), s = std::sin(a);
    const double cx = frame.width / 2.0, cy = frame.height / 2.0;
    const int w = int(std::lround(std::abs(frame.width * c) + std::abs(frame.height * s)));
    const int h = int(std::lround(std::abs(frame.width * s) + std::abs(frame.height * c)));
    JPFrame out = frame;
    out.width = w;
    out.height = h;
    out.rgba.assign(size_t(w) * size_t(h) * kBytesPerPixel, 0);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            // Back from the new picture's pixel to the old one's.
            // (Pixel centres: a pixel's middle is half a pixel in.)
            const double dx = x + 0.5 - w / 2.0, dy = y + 0.5 - h / 2.0;
            sample(frame, cx + c * dx - s * dy - 0.5, cy + s * dx + c * dy - 0.5,
                   &out.rgba[(size_t(y) * size_t(w) + size_t(x)) * kBytesPerPixel]);
        }
    frame = std::move(out);
}

void JPImageTransform::offset(JPFrame& frame, int dx, int dy) {
    if (!whole(frame) || (dx == 0 && dy == 0)) return;
    std::vector<uint8_t> moved(frame.rgba.size(), 0);
    for (int y = 0; y < frame.height; ++y) {
        const int from = y - dy;
        if (from < 0 || from >= frame.height) continue;
        for (int x = 0; x < frame.width; ++x) {
            const int fx = x - dx;
            uint8_t* to = &moved[(size_t(y) * size_t(frame.width) + size_t(x)) * kBytesPerPixel];
            if (fx < 0 || fx >= frame.width) {
                to[3] = 255;
                continue;
            }
            std::memcpy(to, &frame.rgba[(size_t(from) * size_t(frame.width) + size_t(fx)) * kBytesPerPixel], kBytesPerPixel);
        }
    }
    for (int y = 0; y < frame.height; ++y) {
        const int from = y - dy;
        if (from >= 0 && from < frame.height) continue;
        for (int x = 0; x < frame.width; ++x) moved[(size_t(y) * size_t(frame.width) + size_t(x)) * kBytesPerPixel + 3] = 255;
    }
    frame.rgba = std::move(moved);
}

void JPImageTransform::flip(JPFrame& frame, bool flipX, bool flipY) {
    if (!whole(frame) || (!flipX && !flipY)) return;
    std::vector<uint8_t> flipped(frame.rgba.size());
    for (int y = 0; y < frame.height; ++y)
        for (int x = 0; x < frame.width; ++x) {
            const int fy = flipX ? frame.height - 1 - y : y, fx = flipY ? frame.width - 1 - x : x;
            std::memcpy(&flipped[(size_t(y) * size_t(frame.width) + size_t(x)) * kBytesPerPixel],
                        &frame.rgba[(size_t(fy) * size_t(frame.width) + size_t(fx)) * kBytesPerPixel], kBytesPerPixel);
        }
    frame.rgba = std::move(flipped);
}

} // inline namespace jf
