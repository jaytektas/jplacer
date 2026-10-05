// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include <functional>

inline namespace jf {

// What a vision settings page's test buttons work with, from the machine:
// the angle a part is tested at (the machine's test alignment angle), and
// whether it is centred after the test. Without them the tests are not offered.
struct JPVisionTests {
    std::function<double()>     angle;
    std::function<void(double)> setAngle;
    std::function<bool()>       center;
    std::function<void(bool)>   setCenter;
};

} // inline namespace jf
