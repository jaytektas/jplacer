// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPRotationMode.h"

#include <cmath>

inline namespace jf {

namespace {
// Kept within -lim..lim, as OpenPnP's angleNorm.
double norm(double v, double lim) {
    while (std::abs(v) > lim) v += v < 0 ? 2 * lim : -2 * lim;
    return v;
}
} // namespace

std::optional<double> JPRotationMode::offset(const JPJobMachine::Nozzle& n, std::optional<double> axisNow, double pickAngle,
                                             double placeAngle) {
    if (n.rotationMode == "PlacementAngle") return placeAngle;
    if (n.rotationMode == "MinimalRotation") return axisNow ? norm(pickAngle - *axisNow, 180) : 0.0;
    if (n.rotationMode == "LimitedArticulation") {
        const double articulation = n.rotationHigh - n.rotationLow;
        const double pickToPlace = norm(placeAngle - pickAngle, 180);
        const double tolerance = n.maxPickArticulation + n.maxAlignArticulation;
        const double maximum = pickToPlace + (pickToPlace > 0 ? 1 : pickToPlace < 0 ? -1 : 0) * tolerance;
        double start;
        if (std::abs(maximum) < articulation) {
            // Room enough: about the middle of the range.
            start = (n.rotationLow + n.rotationHigh) * 0.5 - maximum * 0.5;
        } else if (pickToPlace > 0) {
            start = n.rotationLow + (articulation - pickToPlace) * n.maxPickArticulation / tolerance;
        } else {
            start = n.rotationHigh - (articulation + pickToPlace) * n.maxPickArticulation / tolerance;
        }
        return norm(pickAngle - start, 180);
    }
    return std::nullopt;   // AbsolutePartAngle
}

std::optional<double> JPRotationMode::prepare(JPJobMachine& machine, const std::string& nozzleId, double pickAngle,
                                              double placeAngle) {
    for (const JPJobMachine::Nozzle& n : machine.nozzles())
        if (n.id == nozzleId) {
            const std::optional<double> o = offset(n, machine.nozzleRotation(nozzleId), pickAngle, placeAngle);
            machine.setRotationModeOffset(nozzleId, o);
            return o;
        }
    return std::nullopt;
}

} // inline namespace jf
