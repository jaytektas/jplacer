// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include <j/config/Json.h>

#include <string>
#include <vector>

inline namespace jf {

// A nozzle's placement check, the whole chain at once, and its pieces. The round test object is first picked off the
// primary fiducial and put back without turning a few times (no bottom vision): how far it moves each time is the
// pick and the let-go alone (the nozzle's offsets cancel), the drift. Then, at angles round the circle, it is
// aligned on the camera looking up as a job aligns a part (turned to the angle first), put back on the fiducial with
// the job's correction, and found there with the head camera: each placement's error is where it landed less where
// it was put. Over the angles the errors split into a bias (their mean) and a part that turns with the nozzle (a
// vector turned by each angle: runout, the rotation's centre), and what neither explains. Where the object truly
// sits on the nozzle turns with the angle too, so bottom vision's offsets' mean is its own bias. The bias is
//   (the nozzle's offset error at the place height) - (bottom vision's bias) + (the drift),
// so the offset error is what is left of it.
struct JPPlacementCheck {
    struct Place {
        double angle = 0;              // the placement's angle
        double offsetX = 0, offsetY = 0;   // where bottom vision saw it on the nozzle
        double errorX = 0, errorY = 0;     // where it landed less where it was put
    };

    struct Drift { double dx = 0, dy = 0; };   // one pick and put back, unturned: how far it moved

    std::string        when;
    double             z = 0;   // where it was put down (its top)
    std::vector<Drift> drifts;
    std::vector<Place> places;

    double biasX() const;
    double biasY() const;
    // The part turning with the angle: at angle a, (turnX, turnY) turned by a.
    void   turning(double& x, double& y) const;
    // What is left of a place's error past the bias and the turning part (mm).
    double residual(const Place& p) const;
    double worstResidual() const;
    // The pieces: the drift's mean, bottom vision's bias (its offsets' mean), and the nozzle's offset error at the
    // place height (the bias less the drift, plus vision's bias).
    double driftX() const;
    double driftY() const;
    double visionBiasX() const;
    double visionBiasY() const;
    double offsetErrorX() const { return biasX() + visionBiasX() - driftX(); }
    double offsetErrorY() const { return biasY() + visionBiasY() - driftY(); }

    static JPPlacementCheck fromJson(const JJson& j);
    JJson toJson() const;
};

} // inline namespace jf
