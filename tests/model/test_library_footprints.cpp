// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// Footprints as the library's own (DESIGN.md, Footprint): land patterns of a package, several to one, each
// known by the names CAD files give it; a board's footprint finds its footprint and through it its package; a
// library of before footprints has its packages' CAD names moved onto footprints; kept whole in library.db; a
// board's copy of a part carries its footprint, and a change to the land pattern (not a name) is one to review.
// A footprint in another unit keeps its size: its numbers converted.
// Tests check with assert(); a Release build must not compile it away.
#undef NDEBUG
#include <cassert>
#include <cmath>

#include "model/JPConfiguration.h"

#include <filesystem>

using namespace jf;
namespace fs = std::filesystem;

int main() {
    const fs::path dir = fs::temp_directory_path() / "jplacer-test-library-footprints";
    fs::remove_all(dir);
    fs::create_directories(dir);
    // A library as schema 2 left it: a package known by a CAD footprint name.
    {
        JPConfiguration c(dir.string());
        std::vector<std::string> problems;
        std::string error;
        assert(c.load(problems, error));
        auto k = std::make_shared<JPPackage>();
        k->id = "R0603";
        k->footprint.bodyWidth = 1.6;
        c.addPackage(k);
        assert(c.save(error));
        JDatabase db;
        assert(db.open((dir / JPLibraryStore::kFile).string()));
        assert(db.exec("INSERT INTO package_akas(package_uuid, text) VALUES(?, ?)",
                       { JDatabase::JBind::from(k->uuid), JDatabase::JBind::from("R_0603_1608Metric") }));
        assert(db.exec("DELETE FROM footprints"));
    }
    {
        JPConfiguration c(dir.string());
        std::vector<std::string> problems;
        std::string error;
        assert(c.load(problems, error));
        // Moved onto a footprint of the package, made from its own.
        assert(c.footprints().size() == 1);
        JPLibraryFootprint* f = c.footprints()[0].get();
        assert(f->packageId == "R0603" && f->name == "R0603" && f->cadNames.size() == 1 && f->geometry.bodyWidth == 1.6);
        assert(c.packageNamed("r_0603_1608metric") == c.package("R0603"));
        // A second footprint of it, from a CAD library, its pads and zero rotation.
        auto kicad = std::make_shared<JPLibraryFootprint>();
        kicad->name = "RESC1608X55N";
        kicad->packageId = "R0603";
        kicad->cadNames = { "Resistor_SMD:R_0603_1608Metric_Pad0.98x0.95mm_HandSolder" };
        kicad->zeroRotationDeg = 90;
        kicad->source = "R_0603.kicad_mod";
        JPFootprint::Pad pad;
        pad.name = "1";
        pad.x = -0.8;
        pad.width = 0.9;
        pad.height = 0.95;
        pad.mark = true;
        kicad->geometry.pads.push_back(pad);
        c.addFootprint(kicad);
        assert(c.footprintsOf("R0603").size() == 2 && c.footprintNamed("resc1608x55n") == kicad.get());
        assert(c.save(error));
    }
    {
        JPConfiguration c(dir.string());
        std::vector<std::string> problems;
        std::string error;
        assert(c.load(problems, error) && c.footprints().size() == 2);
        const JPLibraryFootprint* f = c.footprintNamed("Resistor_SMD:R_0603_1608Metric_Pad0.98x0.95mm_HandSolder");
        assert(f && f->zeroRotationDeg == 90 && f->source == "R_0603.kicad_mod" && f->geometry.pads.size() == 1
               && f->geometry.pads[0].mark && std::abs(f->geometry.pads[0].x + 0.8) < 1e-9);

        // A board's part of that footprint: placed with the footprint its CAD name names.
        auto r = std::make_shared<JPPart>();
        r->id = "R0603-10k";
        r->packageId = "R0603";
        c.addPart(r);
        JPBoardPart bp;
        bp.state = JPBoardPart::State::Matched;
        bp.libraryPartId = "R0603-10k";
        bp.fields["footprint"] = "RESC1608X55N";
        const JPLibraryFootprint* named = c.footprintFor(bp, *r);
        assert(named && named->name == "RESC1608X55N" && named->zeroRotationDeg == 90);
        // A footprint named by no CAD name of the board: the package's first.
        JPBoardPart other = bp;
        other.fields["footprint"] = "something else";
        assert(c.footprintFor(other, *r) && c.footprintFor(other, *r)->name == "R0603");
    }
    fs::remove_all(dir);
    // In another unit, the same size.
    {
        JPFootprint f;
        f.units = JPLengthUnit::Millimeters;
        f.bodyWidth = 2.54;
        f.padPitch = 1.27;
        JPFootprint::Pad pad;
        pad.x = -0.635;
        pad.width = 0.254;
        pad.roundness = 50;
        f.pads.push_back(pad);
        const JPFootprint mils = f.inUnits(JPLengthUnit::Mils);
        assert(mils.units == JPLengthUnit::Mils);
        assert(std::abs(mils.bodyWidth - 100) < 1e-9 && std::abs(mils.padPitch - 50) < 1e-9);
        assert(std::abs(mils.pads[0].x + 25) < 1e-9 && std::abs(mils.pads[0].width - 10) < 1e-9);
        assert(mils.pads[0].roundness == 50);
        const JPFootprint back = mils.inUnits(JPLengthUnit::Millimeters);
        assert(std::abs(back.bodyWidth - 2.54) < 1e-9 && std::abs(back.pads[0].x + 0.635) < 1e-9);
    }
    return 0;
}
