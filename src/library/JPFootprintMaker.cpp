// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPFootprintMaker.h"

inline namespace jf {

namespace {

JPPad pad(int number, double x, double y, double w, double h) {
    return JPPad { std::to_string(number), x, y, w, h, 0, 0 };
}

} // namespace

JPFootprint JPFootprintMaker::dual(const std::string& name, const Dual& d) {
    JPFootprint f;
    f.name = name;
    f.pin1 = "1";
    f.bodyWidth = d.bodyWidth;
    f.bodyLength = d.bodyLength;
    const int perSide = d.pins / 2;
    const double top = (perSide - 1) * d.pitch / 2;
    for (int i = 0; i < perSide; ++i)   // down the left
        f.pads.push_back(pad(i + 1, -d.span / 2, top - i * d.pitch, d.padLength, d.padWidth));
    for (int i = 0; i < perSide; ++i)   // up the right
        f.pads.push_back(pad(perSide + i + 1, d.span / 2, -top + i * d.pitch, d.padLength, d.padWidth));
    return f;
}

JPFootprint JPFootprintMaker::quad(const std::string& name, const Quad& q) {
    JPFootprint f;
    f.name = name;
    f.pin1 = "1";
    f.bodyWidth = f.bodyLength = q.bodySize;
    const int n = q.pinsPerSide;
    const double first = (n - 1) * q.pitch / 2;
    const double out = q.span / 2;
    int number = 1;
    for (int i = 0; i < n; ++i)   // left, top to bottom
        f.pads.push_back(pad(number++, -out, first - i * q.pitch, q.padLength, q.padWidth));
    for (int i = 0; i < n; ++i)   // bottom, left to right
        f.pads.push_back(pad(number++, -first + i * q.pitch, -out, q.padWidth, q.padLength));
    for (int i = 0; i < n; ++i)   // right, bottom to top
        f.pads.push_back(pad(number++, out, -first + i * q.pitch, q.padLength, q.padWidth));
    for (int i = 0; i < n; ++i)   // top, right to left
        f.pads.push_back(pad(number++, first - i * q.pitch, out, q.padWidth, q.padLength));
    if (q.exposedPad > 0) f.pads.push_back(pad(number, 0, 0, q.exposedPad, q.exposedPad));
    return f;
}

} // inline namespace jf
