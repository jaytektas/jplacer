// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include <j/config/Json.h>

#include <optional>
#include <string>
#include <vector>

inline namespace jf {

// One calibration of a nozzle's X, Y offsets on its head: when, how, the
// offsets before and the ones it found, and (the precise calibration, with
// the test object) each angle's own estimate of the correction at each
// height it was made at: half how far the test object moved, picked at that
// angle and placed turned 180 degrees. The correction at a height is their
// mean; an angle further from it than outlierLimit() is an outlier (a bad
// find, the object slipping on the tip). Made at two heights (the primary and
// secondary fiducials), the two corrections give the nozzle's lean.
struct JPOffsetsRun {
    struct Angle { double angle = 0, dx = 0, dy = 0; };
    // The estimates at one height (the tip's Z as it picked and placed).
    struct Height {
        double             z = 0;
        std::vector<Angle> angles;

        double meanX() const;
        double meanY() const;
        // How far angle `a`'s estimate is from their mean (mm).
        double distance(const Angle& a) const;
        // The furthest from their mean (0: none).
        double spread() const;
        double outlierLimit() const;
        bool   outlier(const Angle& a) const { return distance(a) > outlierLimit(); }
        int    outliers() const;
    };

    std::string           when, how;
    double                beforeX = 0, beforeY = 0, afterX = 0, afterY = 0;   // the offsets at `first.z`
    Height                first;
    std::optional<Height> second;
    // The lean found (two heights: X and Y per mm of Z), or kept (one).
    double                leanX = 0, leanY = 0;

    // How it was calibrated (`how`), in words.
    static constexpr const char* kPrecise = "precisely, with the test object";
    static constexpr const char* kOnFiducial = "roughly, on the primary fiducial";
    static constexpr const char* kByMark = "by a mark the nozzle left";

    // How many runs a nozzle keeps, the newest last.
    static constexpr size_t kKept = 10;
    // The outlier limit: this many times the angles' median distance from
    // their mean, and never less than kLeastOutlierMm (so a run whose angles
    // agree to the micron does not call a few microns an outlier).
    static constexpr double kOutlierFactor = 3;
    static constexpr double kLeastOutlierMm = 0.01;
    // A lean beyond this (mm of X or Y per mm of Z) is worth looking at: a bent tip, a loose Z carriage.
    static constexpr double kNotableLean = 0.02;

    double changeX() const { return afterX - beforeX; }
    double changeY() const { return afterY - beforeY; }
    double change() const;
    double spread() const;   // the furthest estimate from its height's mean, both heights
    int    outliers() const;
    size_t angles() const { return first.angles.size() + (second ? second->angles.size() : 0); }

    static JPOffsetsRun fromJson(const JJson& j);
    JJson toJson() const;
};

} // inline namespace jf
