// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPLibrarySync.h"

#include "JPEntryFields.h"

#include <algorithm>

inline namespace jf {

namespace {

using K = JPEntry::Kind;

const JPOrigin* originOf(const JPPartsStore& s, const JPEntry& e) {
    if (e.kind == K::Part) return s.part(e.id) ? &s.part(e.id)->origin : nullptr;
    if (e.kind == K::Package) return s.package(e.id) ? &s.package(e.id)->origin : nullptr;
    return s.footprint(e.id) ? &s.footprint(e.id)->origin : nullptr;
}

int revisionOf(const JPPartsStore& s, const JPEntry& e) {
    if (e.kind == K::Part) return s.part(e.id) ? s.part(e.id)->revision : 0;
    if (e.kind == K::Package) return s.package(e.id) ? s.package(e.id)->revision : 0;
    return s.footprint(e.id) ? s.footprint(e.id)->revision : 0;
}

// The job entry now a copy of library entry `lib`, as of now.
template <typename T>
void remember(T& jobEntry, const T& libEntry) {
    jobEntry.origin = JPOrigin { libEntry.id, libEntry.revision, jobEntry.revision };
}

// The names of `want` that no other package in `s` has.
std::vector<std::string> freeNames(const JPPartsStore& s, const std::vector<std::string>& want, const std::string& selfId) {
    std::vector<std::string> out;
    for (const std::string& n : want) {
        const JPPackage* owner = s.packageNamed(n);
        if ((!owner || owner->id == selfId) && std::find(out.begin(), out.end(), n) == out.end()) out.push_back(n);
    }
    return out;
}

} // namespace

JPEntry JPLibrarySync::counterpart(const JPPartsStore& job, const JPEntry& e, const JPPartsStore& library) {
    JPEntry none { e.kind, {} };
    if (e.kind == K::Part) {
        const JPPart* p = job.part(e.id);
        if (!p) return none;
        if (library.part(p->origin.libraryId)) return { e.kind, p->origin.libraryId };
        for (const JPSupplierNumber& n : p->supplierNumbers)
            if (const JPPart* l = library.partByNumber(n)) return { e.kind, l->id };
        if (const JPPart* l = library.partByMpn(p->mpn)) return { e.kind, l->id };
        return none;
    }
    if (e.kind == K::Package) {
        const JPPackage* k = job.package(e.id);
        if (!k) return none;
        if (library.package(k->origin.libraryId)) return { e.kind, k->origin.libraryId };
        if (const JPPackage* l = library.packageNamed(k->name)) return { e.kind, l->id };
        for (const std::string& n : k->names)
            if (const JPPackage* l = library.packageNamed(n)) return { e.kind, l->id };
        return none;
    }
    const JPFootprint* f = job.footprint(e.id);
    if (!f) return none;
    if (library.footprint(f->origin.libraryId)) return { e.kind, f->origin.libraryId };
    for (const JPFootprint& l : library.footprints)
        if (l.name == f->name) return { e.kind, l.id };
    return none;
}

bool JPLibrarySync::libraryNewer(const JPPartsStore& job, const JPEntry& e, const JPPartsStore& library) {
    const JPOrigin* o = originOf(job, e);
    if (!o || !o->fromLibrary()) return false;
    const int rev = revisionOf(library, { e.kind, o->libraryId });
    return rev > o->libraryRevision;
}

bool JPLibrarySync::changedInJob(const JPPartsStore& job, const JPEntry& e) {
    const JPOrigin* o = originOf(job, e);
    return o && o->fromLibrary() && revisionOf(job, e) != o->copiedAtRevision;
}

void JPLibrarySync::update(JPPartsStore& job, const JPEntry& e, const JPPartsStore& library) {
    const JPOrigin* o = originOf(job, e);
    if (!o || !o->fromLibrary()) return;
    const std::string libId = o->libraryId;
    if (e.kind == K::Part) {
        const JPPart* l = library.part(libId);
        if (!l) return;
        const std::string packageId = l->packageId.empty() ? std::string() : job.copyPackage(library, l->packageId);
        JPPart* j = job.part(e.id);
        const int rev = j->revision + 1;
        *j = *l;
        j->id = e.id;
        j->packageId = packageId;
        j->revision = rev;
        remember(*j, *l);
    } else if (e.kind == K::Package) {
        const JPPackage* l = library.package(libId);
        if (!l) return;
        const std::string footprintId = l->footprintId.empty() ? std::string() : job.copyFootprint(library, l->footprintId);
        JPPackage* j = job.package(e.id);
        std::vector<std::string> names = j->names;   // what the job learned stays
        names.insert(names.end(), l->names.begin(), l->names.end());
        const int rev = j->revision + 1;
        *j = *l;
        j->id = e.id;
        j->footprintId = footprintId;
        j->names = freeNames(job, names, e.id);
        j->revision = rev;
        remember(*j, *l);
    } else {
        const JPFootprint* l = library.footprint(libId);
        JPFootprint* j = job.footprint(e.id);
        if (!l || !j) return;
        const int rev = j->revision + 1;
        *j = *l;
        j->id = e.id;
        j->revision = rev;
        remember(*j, *l);
    }
}

void JPLibrarySync::keep(JPPartsStore& job, const JPEntry& e, const JPPartsStore& library) {
    JPOrigin* o = const_cast<JPOrigin*>(originOf(job, e));
    if (!o || !o->fromLibrary()) return;
    o->libraryRevision = revisionOf(library, { e.kind, o->libraryId });
}

std::vector<std::string> JPLibrarySync::wouldChange(const JPPartsStore& job, const JPEntry& e, const JPPartsStore& library) {
    const JPEntry c = counterpart(job, e, library);
    if (c.id.empty()) return {};
    std::vector<std::string> d = JPEntryFields::differences(library, c, job, e);
    if (d.empty()) d.push_back("nothing: the job's is the same");
    return d;
}

void JPLibrarySync::copyToLibrary(JPPartsStore& job, const JPEntry& e, JPPartsStore& library) {
    const JPEntry c = counterpart(job, e, library);
    if (e.kind == K::Footprint) {
        JPFootprint* j = job.footprint(e.id);
        if (!j) return;
        JPFootprint copy = *j;
        copy.origin = JPOrigin();
        if (JPFootprint* l = library.footprint(c.id)) {
            copy.id = l->id;
            copy.revision = l->revision + 1;
            *l = copy;
            remember(*j, *l);
        } else {
            copy.id.clear();
            copy.revision = 1;
            const std::string id = library.add(std::move(copy));
            remember(*j, *library.footprint(id));
        }
        return;
    }
    if (e.kind == K::Package) {
        JPPackage* j = job.package(e.id);
        if (!j) return;
        std::string footprintId;
        if (!j->footprintId.empty()) {
            const JPEntry fe { K::Footprint, j->footprintId };
            if (counterpart(job, fe, library).id.empty()) copyToLibrary(job, fe, library);
            footprintId = counterpart(job, fe, library).id;
        }
        j = job.package(e.id);
        JPPackage copy = *j;
        copy.origin = JPOrigin();
        copy.footprintId = footprintId;
        if (JPPackage* l = library.package(c.id)) {
            copy.id = l->id;
            copy.revision = l->revision + 1;
            copy.names = freeNames(library, j->names, l->id);
            *l = copy;
            remember(*j, *l);
        } else {
            copy.id.clear();
            copy.revision = 1;
            copy.names.clear();
            const std::string id = library.add(std::move(copy));
            library.package(id)->names = freeNames(library, j->names, id);
            remember(*j, *library.package(id));
        }
        return;
    }
    JPPart* j = job.part(e.id);
    if (!j) return;
    std::string packageId;
    if (!j->packageId.empty()) {
        const JPEntry ke { K::Package, j->packageId };
        if (counterpart(job, ke, library).id.empty()) copyToLibrary(job, ke, library);
        packageId = counterpart(job, ke, library).id;
    }
    j = job.part(e.id);
    JPPart copy = *j;
    copy.origin = JPOrigin();
    copy.packageId = packageId;
    if (JPPart* l = library.part(c.id)) {
        copy.id = l->id;
        copy.revision = l->revision + 1;
        *l = copy;
        remember(*j, *l);
    } else {
        copy.id.clear();
        copy.revision = 1;
        const std::string id = library.add(std::move(copy));
        remember(*j, *library.part(id));
    }
}

std::string JPLibrarySync::bringIntoJob(JPPartsStore& job, const JPEntry& e, const JPPartsStore& library) {
    if (e.kind == K::Part) return job.copyPart(library, e.id);
    if (e.kind == K::Package) return job.copyPackage(library, e.id);
    return job.copyFootprint(library, e.id);
}

} // inline namespace jf
