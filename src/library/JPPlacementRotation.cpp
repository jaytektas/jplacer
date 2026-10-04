// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPPlacementRotation.h"

#include <cmath>

inline namespace jf {

namespace {

constexpr double kSameDeg = 1e-6;

bool same(double a, double b) {
    const double d = std::fmod(std::abs(a - b), 360.0);
    return d < kSameDeg || 360.0 - d < kSameDeg;
}

} // namespace

double JPPlacementRotation::normal(double degrees) {
    double d = std::fmod(degrees, 360.0);
    if (d < 0) d += 360.0;
    if (360.0 - d < kSameDeg) d = 0;
    return d;
}

JPPlacementRotation JPPlacementRotation::of(const JPPlacement& p, const JPPartsStore& job) {
    double turn = 0;
    if (const JPPart* part = job.part(p.partId))
        if (const JPPackage* k = job.package(part->packageId)) turn = k->turnDeg;
    const double fromPart = p.rotationDeg + turn;
    JPPlacementRotation r;
    if (p.rotationSet && !same(p.rotationSetDeg, fromPart)) {
        r.degrees = normal(p.rotationSetDeg);
        r.source = same(p.rotationSetDeg, p.rotationDeg) ? Source::AsImported : Source::Unique;
        return r;
    }
    r.degrees = normal(fromPart);
    r.source = same(turn, 0) ? Source::AsImported : Source::AsPart;
    return r;
}

const char* JPPlacementRotation::name(Source s) {
    switch (s) {
        case Source::AsImported: return "as imported";
        case Source::AsPart:     return "as its part";
        case Source::Unique:     return "unique";
    }
    return "";
}

} // inline namespace jf
