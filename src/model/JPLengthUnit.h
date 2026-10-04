// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include <string>

inline namespace jf {

// The units a length is given in, as OpenPnP names them (its files hold
// these names: "Millimeters", "Inches"…).
enum class JPLengthUnit { Meters, Centimeters, Millimeters, Feet, Inches, Mils, Microns };

} // inline namespace jf
