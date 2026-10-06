// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

inline namespace jf {

// The units a volume is given in, as OpenPnP's VolumeUnit names them. Milli-, micro- and femtolitres are the
// preferred names of the cubic centimetre, millimetre and micron.
enum class JPVolumeUnit {
    CubicMeters, CubicCentimeters, MilliLiters, CubicMillimeters, MicroLiters, CubicFeet, CubicInches, CubicMils,
    CubicMicrons, FemtoLiters
};

} // inline namespace jf
