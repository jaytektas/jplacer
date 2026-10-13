// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPEasyEdaModel.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <sstream>

inline namespace jf {

bool JPEasyEdaModel::height(const std::string& obj, double rotationX, double rotationY, double& heightMm, std::string& why) {
    const double ax = rotationX * std::numbers::pi / 180, ay = rotationY * std::numbers::pi / 180;
    const double cx = std::cos(ax), sx = std::sin(ax), cy = std::cos(ay), sy = std::sin(ay);
    double low = 1e300, high = -1e300;
    std::istringstream in(obj);
    std::string line;
    while (std::getline(in, line)) {
        if (line.size() < 2 || line[0] != 'v' || line[1] != ' ') continue;
        std::istringstream v(line.substr(2));
        double x = 0, y = 0, z = 0;
        if (!(v >> x >> y >> z)) continue;
        // About X, then about Y: only Z is wanted.
        const double z1 = y * sx + z * cx;
        const double z2 = -x * sy + z1 * cy;
        low = std::min(low, z2);
        high = std::max(high, z2);
    }
    if (high < low) {
        why = "its 3D model has no vertices";
        return false;
    }
    heightMm = high - low;
    return true;
}

} // inline namespace jf
