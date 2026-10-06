// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include <j/config/Json.h>

#include <string>
#include <vector>

inline namespace jf {

// A nozzle tip's runout on one nozzle, as OpenPnP's ReferenceNozzleTipCalibration keeps it: its end does not sit
// on the rotation axis, so turning the nozzle swings it round a circle. Measured with the camera looking up, the
// tip's centre at each of several angles (offsets from where the nozzle was sent, mm, JPRunoutFit), and kept as
// its compensation algorithm says:
//
//   the model:  offset(a) = centre + radius (cos(a - phase), sin(a - phase))
//   a table:    the measured offsets, interpolated between their angles
//
// What a move is sent the other way (offset), and how far the camera looking up is taken to be off for this
// nozzle (cameraOffset), are the algorithm's:
//   Model, ModelAffine                     the whole offset; the camera as it is (the axis is off: the nozzle's offset)
//   ModelNoOffset, ModelNoOffsetAffine     the swing alone; the centre only shown (the camera's position error)
//   ModelCameraOffset, ModelCameraOffsetAffine
//                                          the swing alone; the camera taken to be off by the centre for this nozzle
//   Table                                  the interpolated offset; the camera as it is
// (the Affine ones differ in how they are fitted).
struct JPRunout {
    struct Point { double angle = 0, dx = 0, dy = 0; };

    std::string        algorithm = kDefaultAlgorithm;
    double             centreX = 0, centreY = 0;
    double             radius = 0, phaseDeg = 0;
    double             rmsMm = 0, peakMm = 0;   // the measurements against the model
    std::string        when;
    std::vector<Point> points;                  // as measured, in turn (a Table's offsets)

    // OpenPnP's RunoutCompensationAlgorithm names, in its order; its default; and a measuring kept before there
    // was a choice (as it was used: the swing alone).
    static const std::vector<std::string>& algorithms();
    static constexpr const char* kDefaultAlgorithm = "ModelCameraOffsetAffine";
    static constexpr const char* kKeptAlgorithm = "ModelNoOffset";
    bool table() const { return algorithm == "Table"; }

    // The swing at angle `a` (degrees): the model's offset less its centre.
    void runoutAt(double a, double& dx, double& dy) const;
    // What a move of the nozzle to angle `a` is sent the other way (OpenPnP's getOffset).
    void offset(double a, double& dx, double& dy) const;
    // How far the camera looking up is from where it is set, for this nozzle (OpenPnP's getCameraOffset).
    void cameraOffset(double& dx, double& dy) const;
    // The model's error against the measurements (rmsMm, peakMm).
    void estimateError();

    static JPRunout fromJson(const JJson& j);
    JJson toJson() const;
};

} // inline namespace jf
