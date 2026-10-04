// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// Editing a part, package or footprint as text; a placement's rotation and
// where it comes from; a job's copies against the library: the library newer,
// Update and Keep, Copy to Library (added at once when new, the differences
// when it would replace), Bring into Job.
// Tests check with assert(); a Release build must not compile it away.
#undef NDEBUG
#include <cassert>

#include "library/JPEntryFields.h"
#include "library/JPLibrarySync.h"
#include "library/JPPlacementRotation.h"

#include <cmath>

using namespace jf;
using K = JPEntry::Kind;

namespace {

JPPartsStore library() {
    JPPartsStore s;
    JPFootprint f;
    f.name = "SOIC-8";
    f.pads = { { "1", -2.475, 1.905, 1.95, 0.6, 0, 0 }, { "2", -2.475, 0.635, 1.95, 0.6, 0, 0 } };
    f.pin1 = "1";
    JPPackage k;
    k.name = "SOIC-8";
    k.height = 1.75;
    k.footprintId = s.add(std::move(f));
    const std::string kid = s.add(std::move(k));
    s.addName(kid, "SOIC-8");
    JPPart p;
    p.mpn = "LM358DT";
    p.packageId = kid;
    s.add(std::move(p));
    return s;
}

} // namespace

int main() {
    // Fields as text.
    {
        JPPartsStore s = library();
        const JPEntry part { K::Part, s.parts[0].id }, pkg { K::Package, s.packages[0].id }, fp { K::Footprint, s.footprints[0].id };
        std::string error;
        assert(JPEntryFields::get(s, part, "package") == "SOIC-8");
        assert(JPEntryFields::set(s, part, "numbers", "LCSC C7950; Digi-Key 497-1591-1-ND; X1", error));
        assert(s.parts[0].supplierNumbers.size() == 3 && s.parts[0].supplierNumbers[1].supplier == "Digi-Key"
               && s.parts[0].supplierNumbers[2].supplier.empty());
        assert(JPEntryFields::get(s, part, "numbers") == "LCSC C7950; Digi-Key 497-1591-1-ND; X1");
        assert(s.parts[0].revision == 2);
        assert(JPEntryFields::set(s, part, "mpn", "LM358DT", error) && s.parts[0].revision == 2);   // unchanged: no revision
        assert(!JPEntryFields::set(s, pkg, "height", "tall", error) && !error.empty() && s.packages[0].height == 1.75);
        assert(JPEntryFields::set(s, pkg, "height", "1.5", error) && s.packages[0].height == 1.5);
        assert(JPEntryFields::set(s, pkg, "speed", "50", error) && s.packages[0].speed == 0.5);
        assert(JPEntryFields::set(s, pkg, "tips", "N08, N14", error) && s.packages[0].tips.size() == 2);
        assert(JPEntryFields::set(s, pkg, "turn", "180", error) && s.packages[0].turnDeg == 180);
        JPPackage other;
        other.name = "SOIC-14";
        const std::string oid = s.add(std::move(other));
        s.addName(oid, "SO-14");
        assert(!JPEntryFields::set(s, pkg, "names", "SOIC-8, SO-14", error) && error.find("SOIC-14") != std::string::npos);
        assert(JPEntryFields::set(s, pkg, "names", "SOIC-8, SO-8", error) && s.packageNamed("SO-8") == &s.packages[0]);
        assert(!JPEntryFields::set(s, fp, "pin1", "9", error) && JPEntryFields::set(s, fp, "pin1", "2", error));
        assert(!JPEntryFields::set(s, part, "package", "SOIC-14", error));   // linked by choosing, not by typing
    }
    // Rotation: imported, plus the package's turn, unless set by hand.
    {
        JPPartsStore s = library();
        JPPlacement p;
        p.partId = s.parts[0].id;
        p.rotationDeg = 90;
        JPPlacementRotation r = JPPlacementRotation::of(p, s);
        assert(r.degrees == 90 && r.source == JPPlacementRotation::Source::AsImported);
        s.packages[0].turnDeg = 180;
        r = JPPlacementRotation::of(p, s);
        assert(r.degrees == 270 && r.source == JPPlacementRotation::Source::AsPart);
        p.rotationSet = true;
        p.rotationSetDeg = 0;
        r = JPPlacementRotation::of(p, s);
        assert(r.degrees == 0 && r.source == JPPlacementRotation::Source::Unique);
        p.rotationSetDeg = -90;   // the same as the part's 270
        r = JPPlacementRotation::of(p, s);
        assert(r.degrees == 270 && r.source == JPPlacementRotation::Source::AsPart);
        assert(JPPlacementRotation::normal(-90) == 270 && JPPlacementRotation::normal(720) == 0);
    }
    // A job's copies and the library.
    {
        JPPartsStore lib = library();
        JPPartsStore job;
        const std::string pid = JPLibrarySync::bringIntoJob(job, { K::Part, lib.parts[0].id }, lib);
        assert(JPLibrarySync::bringIntoJob(job, { K::Part, lib.parts[0].id }, lib) == pid);
        const JPEntry part { K::Part, pid };
        const JPEntry pkg { K::Package, job.part(pid)->packageId };
        assert(!JPLibrarySync::libraryNewer(job, part, lib) && !JPLibrarySync::changedInJob(job, part));

        // The library changes: the job is told, and Keep stops it saying so.
        std::string error;
        assert(JPEntryFields::set(lib, { K::Package, lib.packages[0].id }, "height", "1.6", error));
        assert(JPLibrarySync::libraryNewer(job, pkg, lib) && !JPLibrarySync::libraryNewer(job, part, lib));
        JPLibrarySync::keep(job, pkg, lib);
        assert(!JPLibrarySync::libraryNewer(job, pkg, lib) && job.package(pkg.id)->height == 1.75);
        // Again, and Update takes the library's.
        assert(JPEntryFields::set(lib, { K::Package, lib.packages[0].id }, "height", "1.7", error));
        assert(JPLibrarySync::libraryNewer(job, pkg, lib));
        JPLibrarySync::update(job, pkg, lib);
        assert(!JPLibrarySync::libraryNewer(job, pkg, lib) && job.package(pkg.id)->height == 1.7);
        assert(job.part(pid)->packageId == pkg.id);   // its id, and what links to it, stay

        // Changed in the job, then put into the library: the differences first.
        assert(JPEntryFields::set(job, pkg, "turn", "90", error) && JPLibrarySync::changedInJob(job, pkg));
        const std::vector<std::string> d = JPLibrarySync::wouldChange(job, pkg, lib);
        assert(d.size() == 1 && d[0].find("Turn") == 0);
        const int before = lib.packages[0].revision;
        JPLibrarySync::copyToLibrary(job, pkg, lib);
        assert(lib.packages.size() == 1 && lib.packages[0].turnDeg == 90 && lib.packages[0].revision == before + 1);
        assert(!JPLibrarySync::libraryNewer(job, pkg, lib) && !JPLibrarySync::changedInJob(job, pkg));

        // A part new in the job, in a package new in the job: both added at once.
        JPPackage k;
        k.name = "PowerSSO-16";
        const std::string kid = job.add(std::move(k));
        job.addName(kid, "POWERSSO-16");
        JPPart p;
        p.mpn = "VNQ7140AJTR";
        p.packageId = kid;
        const std::string nid = job.add(std::move(p));
        assert(JPLibrarySync::wouldChange(job, { K::Part, nid }, lib).empty());
        JPLibrarySync::copyToLibrary(job, { K::Part, nid }, lib);
        const JPPart* lp = lib.partByMpn("VNQ7140AJTR");
        assert(lp && lib.package(lp->packageId) && lib.package(lp->packageId)->name == "PowerSSO-16");
        assert(lib.packageNamed("POWERSSO-16") == lib.package(lp->packageId));
        assert(job.part(nid)->origin.libraryId == lp->id && job.package(kid)->origin.libraryId == lp->packageId);
    }
    return 0;
}
