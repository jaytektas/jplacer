// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include <j/config/Json.h>

#include <string>
#include <vector>

inline namespace jf {

// One calibration of a nozzle's X, Y offsets on its head: when, how, the
// offsets before and the ones it found, and (the precise calibration, with
// the test object) each angle's own estimate of the correction: half how far
// the test object moved, picked at that angle and placed turned 180 degrees.
// The correction made is their mean; an angle further from it than
// outlierLimit() is an outlier (a bad find, the object slipping on the tip).
struct JPOffsetsRun {
    struct Angle { double angle = 0, dx = 0, dy = 0; };

    std::string        when, how;
    double             beforeX = 0, beforeY = 0, afterX = 0, afterY = 0;
    std::vector<Angle> angles;

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

    double changeX() const { return afterX - beforeX; }
    double changeY() const { return afterY - beforeY; }
    double change() const;
    // How far angle `a`'s estimate is from the correction made (mm).
    double distance(const Angle& a) const;
    // The angles' furthest from the correction made (0: none).
    double spread() const;
    double outlierLimit() const;
    bool   outlier(const Angle& a) const { return distance(a) > outlierLimit(); }
    int    outliers() const;

    static JPOffsetsRun fromJson(const JJson& j);
    JJson toJson() const;
};

} // inline namespace jf
