// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPPartMaker.h"

#include <vector>

inline namespace jf {

namespace {

// The placement's package names, CAD footprint first.
std::vector<std::string> namesOf(const JPPlacement& p) {
    std::vector<std::string> out;
    for (const std::string* n : { &p.footprint, &p.supplierPackage })
        if (!n->empty()) out.push_back(*n);
    return out;
}

// The package in `store` one of the placement's names belongs to.
const JPPackage* namedPackage(const JPPartsStore& store, const JPPlacement& p) {
    for (const std::string& n : namesOf(p))
        if (const JPPackage* k = store.packageNamed(n)) return k;
    return nullptr;
}

// The job's package for a new part: one already named so in the job, else
// the library's copied in, else a new one of that name, no footprint yet.
std::string packageFor(const JPPlacement& p, const JPPartsStore& library, JPPartsStore& job) {
    if (const JPPackage* k = namedPackage(job, p)) return k->id;
    if (const JPPackage* k = namedPackage(library, p)) return job.copyPackage(library, k->id);
    const std::vector<std::string> names = namesOf(p);
    if (names.empty()) return {};
    JPPackage k;
    k.name = !p.supplierPackage.empty() ? p.supplierPackage : p.footprint;
    const std::string id = job.add(std::move(k));
    for (const std::string& n : names) job.addName(id, n);
    return id;
}

JPPart partFrom(const JPPlacement& p) {
    JPPart part;
    part.mpn             = p.mpn;
    part.manufacturer    = p.manufacturer;
    part.supplierNumbers = p.supplierNumbers;
    part.value           = p.value;
    part.tolerance       = p.tolerance;
    part.voltage         = p.voltage;
    part.power           = p.power;
    part.dielectric      = p.dielectric;
    part.temperature     = p.temperature;
    part.description     = p.description;
    return part;
}

} // namespace

std::string JPPartMaker::fromPlacement(const JPPlacement& p, const JPPartsStore& library, JPPartsStore& job) {
    JPPart part = partFrom(p);
    part.packageId = packageFor(p, library, job);
    return job.add(std::move(part));
}

} // inline namespace jf
