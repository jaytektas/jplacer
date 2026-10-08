// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// Footprints as the library's own (DESIGN.md, Footprint): land patterns of a package, several to one, each
// known by the names CAD files give it; a board's footprint finds its footprint and through it its package; a
// library of before footprints has its packages' CAD names moved onto footprints; kept whole in library.db; a
// board's copy of a part carries its footprint, and a change to the land pattern (not a name) is one to review.
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

        // A board's part of that footprint: its copy carries the footprint the CAD name names.
        auto r = std::make_shared<JPPart>();
        r->id = "R0603-10k";
        r->packageId = "R0603";
        c.addPart(r);
        JPBoardPart bp;
        bp.state = JPBoardPart::State::Matched;
        bp.libraryPartId = "R0603-10k";
        bp.fields["footprint"] = "RESC1608X55N";
        c.takeCopy(bp);
        assert(bp.copyFootprint && bp.copyFootprint->name == "RESC1608X55N" && !c.differs(bp));
        // A CAD name learned is not a change; a pad moved is.
        c.footprintNamed("RESC1608X55N")->cadNames.push_back("R0603_IPC");
        assert(!c.differs(bp));
        c.footprintNamed("RESC1608X55N")->geometry.pads[0].x = -0.85;
        assert(c.differs(bp));
        // Through the board file and back, the footprint with it.
        const JPBoardPart back = JPBoardPart::fromJson(JJson::parse(bp.toJson().dump()));
        assert(back.copyFootprint && back.copyFootprint->zeroRotationDeg == 90 && back.copyFootprint->geometry.pads.size() == 1);
        // A footprint named by no CAD name of the board: the package's first.
        JPBoardPart other = bp;
        other.fields["footprint"] = "something else";
        c.takeCopy(other);
        assert(other.copyFootprint && other.copyFootprint->name == "R0603");
    }
    fs::remove_all(dir);
    return 0;
}
