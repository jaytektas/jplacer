// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// The parts store and the library on disk: lookups by number, MPN and name;
// a name belonging to one package; a library part copied into a job once,
// with its package and footprint, remembering where it came from; saved and
// read back; a library file that cannot be read left untouched and the
// library read-only.
// Tests check with assert(); a Release build must not compile it away.
#undef NDEBUG
#include <cassert>

#include "library/JPLibrary.h"

#include <filesystem>
#include <fstream>
#include <sstream>

#include <unistd.h>

using namespace jf;
namespace fs = std::filesystem;

namespace {

JPPartsStore sample() {
    JPPartsStore s;
    JPFootprint f;
    f.name = "R0603";
    f.pads = { { "1", -0.8, 0, 0.9, 0.95, 0, 0 }, { "2", 0.8, 0, 0.9, 0.95, 0, 0 } };
    f.pin1 = "1";
    const std::string fid = s.add(std::move(f));
    JPPackage k;
    k.name = "0603";
    k.footprintId = fid;
    k.height = 0.45;
    const std::string kid = s.add(std::move(k));
    assert(s.addName(kid, "R0603") && s.addName(kid, "0603"));
    JPPart a;
    a.mpn = "0603WAF1002T5E";
    a.value = "10k";
    a.supplierNumbers = { { "LCSC", "C25804" } };
    a.packageId = kid;
    s.add(std::move(a));
    JPPart b;
    b.value = "1k";
    b.packageId = kid;
    s.add(std::move(b));
    return s;
}

std::string text(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

} // namespace

int main() {
    // Lookups, and a name belonging to one package.
    {
        JPPartsStore s = sample();
        assert(s.partByNumber({ "LCSC", "c25804" }) && s.partByNumber({ "", "C25804" }));
        assert(!s.partByNumber({ "Digi-Key", "C25804" }));
        assert(s.partByMpn("0603 waf1002-t5e") == s.partByNumber({ "LCSC", "C25804" }));
        assert(s.packageNamed("R0603") && s.packageNamed("R0603") == s.packageNamed("0603"));
        JPPackage other;
        other.name = "0402";
        const std::string oid = s.add(std::move(other));
        assert(!s.addName(oid, "R0603") && s.packageNamed("R0603")->name == "0603");
        assert(s.addName(oid, "R0402") && s.packageNamed("R0402")->id == oid);
    }
    // Copying from the library: once each, origins kept, the package shared.
    {
        const JPPartsStore lib = sample();
        JPPartsStore job;
        const std::string a = job.copyPart(lib, lib.parts[0].id);
        const std::string b = job.copyPart(lib, lib.parts[1].id);
        assert(!a.empty() && !b.empty() && a != b && a != lib.parts[0].id);
        assert(job.copyPart(lib, lib.parts[0].id) == a);   // a second time: the copy already there
        assert(job.parts.size() == 2 && job.packages.size() == 1 && job.footprints.size() == 1);
        assert(job.part(a)->packageId == job.part(b)->packageId);
        assert(job.part(a)->origin.libraryId == lib.parts[0].id);
        assert(job.packages[0].origin.libraryId == lib.packages[0].id && job.packages[0].hasName("R0603"));
        assert(job.footprint(job.packages[0].footprintId)->pin1Pad()->x == -0.8);
    }
    // Saved and read back.
    const fs::path dir = fs::temp_directory_path() / ("jplacer-test-library-" + std::to_string(::getpid()));
    fs::remove_all(dir);
    {
        JPLibrary lib;
        assert(lib.open(dir.string()) && !lib.readOnly() && lib.store().parts.empty());
        lib.store() = sample();
        std::string error;
        assert(lib.save(error));
        assert(!fs::exists(lib.path() + ".new"));
        JPLibrary again;
        assert(again.open(dir.string()));
        assert(again.store().parts.size() == 2 && again.store().footprints[0].pads.size() == 2);
        assert(again.store().partByNumber({ "LCSC", "C25804" })->mpn == "0603WAF1002T5E");
        assert(again.store().packageNamed("R0603")->height == 0.45);
    }
    // A damaged file: read-only, and left as it is.
    {
        const std::string path = (dir / JPLibrary::kFileName).string();
        { std::ofstream(path, std::ios::trunc) << "{ \"version\": 1, \"parts\": [ "; }
        const std::string before = text(path);
        JPLibrary lib;
        assert(!lib.open(dir.string()) && lib.readOnly() && !lib.problem().empty() && lib.store().parts.empty());
        std::string error;
        assert(!lib.save(error) && !error.empty());
        assert(text(path) == before);
        // From a newer jplacer: the same.
        { std::ofstream(path, std::ios::trunc) << "{ \"version\": 2 }"; }
        assert(!lib.open(dir.string()) && lib.readOnly());
    }
    fs::remove_all(dir);
    return 0;
}
