// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include <string>

inline namespace jf {

// A part's 3D model as EasyEDA's model store gives it (an OBJ, in millimetres), read for how high it stands: its
// vertices turned as the footprint turns it about X, then Y, and the height across them.
class JPEasyEdaModel {
public:
    // False (and why) when it has no vertices.
    static bool height(const std::string& obj, double rotationX, double rotationY, double& heightMm, std::string& why);
};

} // inline namespace jf
