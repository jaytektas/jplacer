// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPPlacementState.h"

inline namespace jf {

JPPlacementState JPPlacementState::of(const JPPlacement& p, const JPPartsStore& job) {
    using K = Kind;
    if (p.fiducial) return { K::Fiducial, "a fiducial: nothing is placed" };
    if (p.doNotPlace) return { K::DoNotPlace, "do not place, as the files say" };
    const JPPart* part = job.part(p.partId);
    if (!part) return { K::NoPart, "no part" };
    const JPPackage* package = job.package(part->packageId);
    if (!package) return { K::NoPackage, "its part has no package" };
    for (const std::string* n : { &p.footprint, &p.supplierPackage }) {
        const JPPackage* owner = job.packageNamed(*n);
        if (owner && owner != package)
            return { K::Conflict, "its part is in " + package->name + ", but " + *n + " is " + owner->name };
    }
    const JPFootprint* footprint = job.footprint(package->footprintId);
    if (!footprint) return { K::NoFootprint, package->name + " has no footprint" };
    if (p.pins > 0 && !footprint->pads.empty() && int(footprint->pads.size()) != p.pins)
        return { K::Conflict, "the file counts " + std::to_string(p.pins) + " pins; " + footprint->name + " has "
                                  + std::to_string(footprint->pads.size()) + " pads" };
    if (p.partGuessed) return { K::Guess, "matched by value and package only: confirm it" };
    return { K::Ready, "ready" };
}

const char* JPPlacementState::name(Kind kind) {
    switch (kind) {
        case Kind::Fiducial:    return "Fiducial";
        case Kind::DoNotPlace:  return "Do not place";
        case Kind::NoPart:      return "No part";
        case Kind::NoPackage:   return "No package";
        case Kind::NoFootprint: return "No footprint";
        case Kind::Conflict:    return "Conflict";
        case Kind::Guess:       return "Guess";
        case Kind::Ready:       return "Ready";
    }
    return "";
}

} // inline namespace jf
