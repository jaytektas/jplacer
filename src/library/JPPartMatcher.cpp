// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPPartMatcher.h"

#include "JPValue.h"

#include "common/JPlacerLog.h"

#include <j/core/Log.h>

#include <cctype>
#include <map>

inline namespace jf {

namespace {

std::string lower(const std::string& s) {
    std::string out;
    for (const char c : s) out += char(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

// What makes placements the same thing, strongest first.
std::string groupKey(const JPPlacement& p) {
    for (const JPSupplierNumber& n : p.supplierNumbers)
        if (!n.number.empty()) return "n:" + lower(n.supplier) + ":" + lower(n.number);
    if (const std::string m = JPPartsStore::mpnKey(p.mpn); !m.empty()) return "m:" + m;
    if (p.value.empty() && p.footprint.empty() && p.supplierPackage.empty()) return {};
    return "v:" + lower(p.value) + "|" + lower(p.tolerance) + "|" + lower(p.voltage) + "|" + lower(p.power) + "|"
         + lower(p.dielectric) + "|" + lower(p.footprint) + "|" + lower(p.supplierPackage);
}

// A rating both give, equal; one that either leaves out does not count.
bool ratingAgrees(const std::string& a, const std::string& b) {
    return a.empty() || b.empty() || JPValue::same(a, b);
}

bool ratingsAgree(const JPPart& part, const JPPlacement& p) {
    return ratingAgrees(part.tolerance, p.tolerance) && ratingAgrees(part.voltage, p.voltage)
        && ratingAgrees(part.power, p.power) && ratingAgrees(part.dielectric, p.dielectric)
        && ratingAgrees(part.temperature, p.temperature);
}

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

// A part known by number or MPN, in the job already or copied from the library.
std::string certainPart(const JPPlacement& p, const JPPartsStore& library, JPPartsStore& job) {
    for (const JPSupplierNumber& n : p.supplierNumbers) {
        if (const JPPart* part = job.partByNumber(n)) return part->id;
        if (const JPPart* part = library.partByNumber(n)) return job.copyPart(library, part->id);
    }
    if (const JPPart* part = job.partByMpn(p.mpn)) return part->id;
    if (const JPPart* part = library.partByMpn(p.mpn)) return job.copyPart(library, part->id);
    return {};
}

// A library part this could be, from its value, ratings and package alone.
const JPPart* guessedPart(const JPPlacement& p, const JPPartsStore& library) {
    const JPPackage* k = namedPackage(library, p);
    if (!k || p.value.empty()) return nullptr;
    for (const JPPart& part : library.parts)
        if (part.packageId == k->id && JPValue::same(part.value, p.value) && ratingsAgree(part, p)) return &part;
    return nullptr;
}

// The placement's names, where nothing has them yet, given to its part's
// package in the job, so the next placement written that way finds it. A
// name the library gives to another package is that package's: it is
// copied into the job, so the placement shows the conflict.
void learnNames(const JPPlacement& p, const std::string& partId, const JPPartsStore& library, JPPartsStore& job) {
    const JPPart* part = job.part(partId);
    if (!part || part->packageId.empty()) return;
    const std::string packageId = part->packageId;
    const JPPackage* mine = job.package(packageId);
    for (const std::string& n : namesOf(p)) {
        if (job.packageNamed(n)) continue;
        const JPPackage* owner = library.packageNamed(n);
        if (owner && owner->id != mine->origin.libraryId) job.copyPackage(library, owner->id);
        else job.addName(packageId, n);
        mine = job.package(packageId);
    }
}

} // namespace

JPPartMatcher::Result JPPartMatcher::match(std::vector<JPPlacement>& placements, const JPPartsStore& library,
                                           JPPartsStore& job) {
    Result r;
    std::map<std::string, std::vector<JPPlacement*>> groups;
    for (JPPlacement& p : placements) {
        if (p.fiducial || !p.partId.empty()) continue;
        const std::string key = groupKey(p);
        if (key.empty()) ++r.noPart;
        else groups[key].push_back(&p);
    }
    for (auto& [key, members] : groups) {
        const JPPlacement& first = *members.front();
        std::string partId = certainPart(first, library, job);
        bool guessed = false;
        if (!partId.empty()) {
            r.certain += int(members.size());
        } else if (const JPPart* g = guessedPart(first, library)) {
            partId = job.copyPart(library, g->id);
            guessed = true;
            r.guessed += int(members.size());
        } else {
            JPPart part = partFrom(first);
            part.packageId = packageFor(first, library, job);
            partId = job.add(std::move(part));
            ++r.created;
        }
        learnNames(first, partId, library, job);
        for (JPPlacement* p : members) {
            p->partId = partId;
            p->partGuessed = guessed;
        }
    }
    JLOGC(JPlacerLog::kLibrary, JLogLevel::Info) << "parts matched: " << r.certain << " certain, " << r.guessed
                                                 << " guessed, " << r.created << " part(s) new to the job, "
                                                 << r.noPart << " placement(s) with nothing to go on";
    return r;
}

} // inline namespace jf
