// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include <j/config/Json.h>

inline namespace jf {

// How a board's fiducials are measured (JPBoardLocator). Each is found and
// the camera moved over where it was found, pass after pass, until a pass
// moves it less than `centredMm` or `passes` have been made.
//
// With a parallax diameter, each pass looks at the fiducial from two places
// instead of from straight above it: `parallaxDiameterMm` apart, either side
// of it along `parallaxAngleDeg` (0 along X, 90 along Y), and takes the
// midpoint of the two. A shiny (HASL) fiducial seen straight on mirrors the
// camera through its light's diffuser and can look dark or misshapen; seen
// from the side it mirrors the bright diffuser, and the two sides' errors
// cancel. 0: straight above.
struct JPFiducialConfig {
    double parallaxDiameterMm = 0;
    double parallaxAngleDeg   = 0;
    int    passes             = 4;
    double centredMm          = 0.01;

    static constexpr int kMostPasses = 20;

    static JPFiducialConfig fromJson(const JJson& j) {
        JPFiducialConfig f;
        f.parallaxDiameterMm = j["parallaxDiameter"].number(f.parallaxDiameterMm);
        f.parallaxAngleDeg   = j["parallaxAngle"].number(f.parallaxAngleDeg);
        f.passes             = int(j["passes"].number(f.passes));
        f.centredMm          = j["centredTo"].number(f.centredMm);
        return f;
    }
    JJson toJson() const {
        JJson j = JJson::object();
        j["parallaxDiameter"] = parallaxDiameterMm;
        j["parallaxAngle"]    = parallaxAngleDeg;
        j["passes"]           = passes;
        j["centredTo"]        = centredMm;
        return j;
    }
};

} // inline namespace jf
