// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// A KiCad board read as a placement file (JPKicadBoardFile), checked against KiCad itself: the grblHAL
// controller board (KiCad 9, its footprints kept, the rest of the board left out) and the same board with
// every footprint flipped to the bottom (by KiCad's pcbnew), each against the .pos KiCad 10's kicad-cli
// writes of it (--units mm --use-drill-file-origin): the same placements, positions, rotations and sides
// (a value with spaces kept, where the .pos writes them as "_").
// Their footprints: the flipped board's the same as the top one's, pad for pad (the rotation and mirror
// KiCad writes on a board taken off), and one the same as its library's .kicad_mod. A KiCad 5 board; a
// footprint edited on the board kept as a second; Do Not Populate; and through the CPL and BOM import, the
// board's own parts' packages given the footprints.
// Tests check with assert(); a Release build must not compile it away.
#undef NDEBUG
#include <cassert>

#include "import/JPCplBomImport.h"
#include "import/JPKicadBoardFile.h"
#include "model/JPKicadModImporter.h"

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <map>

using namespace jf;
using I = JPImportField::Id;
namespace fs = std::filesystem;

namespace {

const fs::path kData = fs::path(JPLACER_TESTDATA_DIR) / "kicad";

bool near(double a, double b, double within = 1e-6) { return std::fabs(a - b) < within; }

JPImportSource board(const std::string& name) {
    JPImportSource s;
    std::string error;
    const bool ok = JPKicadBoardFile::read((kData / name).string(), s, error);
    if (!ok) std::fprintf(stderr, "%s: %s\n", name.c_str(), error.c_str());
    assert(ok);
    s.guess();
    return s;
}

// Each row against KiCad's .pos of the same board, by designator.
void samePlacements(const JPImportSource& s, const std::string& pos) {
    JPTableFile k;
    std::string error;
    assert(JPTableFile::read((kData / pos).string(), k, error));
    assert(s.table.rows.size() == k.rows.size() && !k.rows.empty());
    std::map<std::string, const std::vector<std::string>*> theirs;
    for (const auto& r : k.rows) theirs[r[0]] = &r;
    for (const auto& r : s.table.rows) {
        const auto it = theirs.find(r[0]);
        assert(it != theirs.end());
        const std::vector<std::string>& t = *it->second;
        std::string value = r[1];   // KiCad's .pos writes a value's spaces as "_", to keep its columns
        for (char& c : value)
            if (c == ' ') c = '_';
        assert(value == t[1]);
        assert(r[2] == t[2]);                                                  // footprint (KiCad's Package)
        assert(near(std::stod(r[3]), std::stod(t[3]), 1e-4) && near(std::stod(r[4]), std::stod(t[4]), 1e-4));
        assert(near(std::fmod(std::stod(r[5]) - std::stod(t[5]) + 720, 360), 0, 1e-4));   // rotation
        assert(r[6] == t[6]);                                                  // side
    }
}

} // namespace

int main() {
    assert(JPKicadBoardFile::is("/x/grblHAL.kicad_pcb") && JPKicadBoardFile::is("B.KICAD_PCB") && !JPKicadBoardFile::is("b.pos"));

    // The grblHAL board, top and every footprint flipped: what KiCad's own position file says.
    const JPImportSource top = board("grblhal.kicad_pcb");
    const JPImportSource flipped = board("grblhal-flipped.kicad_pcb");
    samePlacements(top, "grblhal.pos");
    samePlacements(flipped, "grblhal-flipped.pos");
    // Its columns read as a .pos's.
    assert(top.column(I::Designator) == 0 && top.column(I::Value) == 1 && top.column(I::Footprint) == 2);
    assert(top.column(I::X) == 3 && top.column(I::Y) == 4 && top.column(I::Rotation) == 5 && top.column(I::Side) == 6);
    assert(top.column(I::DoNotPlace) == 7 && top.units == JPLengthUnit::Millimeters);
    // The four mounting holes KiCad leaves out of its position file are left out; the one DNP part says so.
    int dnp = 0;
    for (const auto& r : top.table.rows) dnp += r[7] == "yes";
    assert(dnp == 1);

    // Each footprint drawn once, flipped or not the same, pad for pad: the placement's turn and mirror taken off.
    assert(!top.footprints.empty() && top.footprints.size() == flipped.footprints.size() && top.notes.empty());
    for (const auto& [name, fp] : top.footprints) {
        const auto f = flipped.footprints.find(name);
        assert(f != flipped.footprints.end() && f->second.pads.size() == fp.pads.size());
        for (size_t i = 0; i < fp.pads.size(); ++i) {
            const JPFootprint::Pad &a = fp.pads[i], &b = f->second.pads[i];
            assert(a.name == b.name && near(a.x, b.x) && near(a.y, b.y) && near(a.width, b.width) && near(a.height, b.height));
            assert(near(a.rotation, b.rotation) && near(a.roundness, b.roundness));
        }
    }
    // An LQFP-100's pads as KiCad's library draws them: a hundred, pad 1 at the top of the left row (Y up), pad 26
    // the left of the bottom row, upright (its size the other way round), none turned.
    const JPFootprint& lqfp = top.footprints.at("LQFP-100_14x14mm_P0.5mm");
    assert(lqfp.pads.size() == 100 && lqfp.pads[0].name == "1" && near(lqfp.pads[0].x, -7.675) && near(lqfp.pads[0].y, 6));
    assert(lqfp.pads[25].name == "26" && near(lqfp.pads[25].x, -6) && near(lqfp.pads[25].y, -7.675));
    assert(near(lqfp.pads[25].width, 0.3) && near(lqfp.pads[25].height, 1.6));
    for (const JPFootprint::Pad& p : lqfp.pads) assert(near(p.rotation, 0));
    // A footprint as its own library draws it (the .kicad_mod beside the board).
    {
        std::vector<JPFootprint::Pad> lib;
        std::string error;
        assert(JPKicadModImporter::read((kData / "RES-ADJ-SMD_G43AT.kicad_mod").string(), lib, error));
        const JPFootprint& onBoard = top.footprints.at("RES-ADJ-SMD_G43AT");
        assert(!lib.empty() && lib.size() == onBoard.pads.size());
        for (size_t i = 0; i < lib.size(); ++i)
            assert(lib[i].name == onBoard.pads[i].name && near(lib[i].x, onBoard.pads[i].x) && near(lib[i].y, onBoard.pads[i].y)
                   && near(lib[i].width, onBoard.pads[i].width) && near(lib[i].rotation, onBoard.pads[i].rotation));
    }

    // KiCad 5: (module …), (fp_text reference …); a virtual one left out; a footprint edited on the board
    // (R2's pad 2 moved) kept as a second, R3 drawn as R1 the first; the drill/place origin; a bottom one.
    {
        const char* v5 = R"((kicad_pcb (version 20171130) (host pcbnew 5.1.9)
  (setup (aux_axis_origin 100 100))
  (module Resistor_SMD:R_0603_1608Metric (layer F.Cu) (tedit 5B301BBD) (tstamp 1)
    (at 110 90 90)
    (fp_text reference R1 (at 0 -1.43 90) (layer F.SilkS))
    (fp_text value 10k (at 0 1.43 90) (layer F.Fab))
    (attr smd)
    (pad 1 smd roundrect (at -0.7875 0 90) (size 0.875 0.95) (layers F.Cu F.Paste F.Mask) (roundrect_rratio 0.25))
    (pad 2 smd roundrect (at 0.7875 0 90) (size 0.875 0.95) (layers F.Cu F.Paste F.Mask) (roundrect_rratio 0.25)))
  (module Resistor_SMD:R_0603_1608Metric (layer F.Cu) (tstamp 2)
    (at 120 90)
    (fp_text reference R2 (at 0 -1.43) (layer F.SilkS))
    (fp_text value 1k (at 0 1.43) (layer F.Fab))
    (pad 1 smd roundrect (at -0.7875 0) (size 0.875 0.95) (layers F.Cu F.Paste F.Mask) (roundrect_rratio 0.25))
    (pad 2 smd roundrect (at 0.9 0) (size 0.875 0.95) (layers F.Cu F.Paste F.Mask) (roundrect_rratio 0.25)))
  (module Resistor_SMD:R_0603_1608Metric (layer B.Cu) (tstamp 3)
    (at 130 95 180)
    (fp_text reference R3 (at 0 1.43 180) (layer B.SilkS))
    (fp_text value 10k (at 0 -1.43 180) (layer B.Fab))
    (pad 1 smd roundrect (at -0.7875 0 180) (size 0.875 0.95) (layers B.Cu B.Paste B.Mask) (roundrect_rratio 0.25))
    (pad 2 smd roundrect (at 0.7875 0 180) (size 0.875 0.95) (layers B.Cu B.Paste B.Mask) (roundrect_rratio 0.25)))
  (module Logo (layer F.Cu) (tstamp 4) (at 0 0)
    (fp_text reference G1 (at 0 0) (layer F.SilkS)) (attr virtual))
))";
        JPImportSource s;
        std::string error;
        assert(JPKicadBoardFile::parse(v5, s, error));
        assert(s.table.rows.size() == 3);
        const auto& r1 = s.table.rows[0];
        assert(r1[0] == "R1" && r1[1] == "10k" && r1[2] == "R_0603_1608Metric" && r1[3] == "10" && r1[4] == "10" && r1[5] == "90");
        assert(r1[6] == "top" && s.table.rows[2][6] == "bottom" && s.table.rows[2][4] == "5");
        assert(s.table.rows[1][2] == "R_0603_1608Metric (2)" && s.table.rows[2][2] == "R_0603_1608Metric");
        assert(s.footprints.size() == 2 && s.notes.size() == 1);
        assert(s.notes[0].find("R2") != std::string::npos && s.notes[0].find("R1") != std::string::npos);
        const JPFootprint& r = s.footprints.at("R_0603_1608Metric");
        assert(r.pads.size() == 2 && near(r.pads[0].x, -0.7875) && near(r.pads[0].rotation, 0) && near(r.pads[0].roundness, 25));
        assert(near(s.footprints.at("R_0603_1608Metric (2)").pads[1].x, 0.9));
    }
    // Not a board: said so.
    {
        JPImportSource s;
        std::string error;
        assert(!JPKicadBoardFile::parse("(footprint \"R\" (layer F.Cu))", s, error) && error.find("Not a KiCad board") == 0);
        assert(!JPKicadBoardFile::parse("(kicad_pcb (version 1)", s, error) && !error.empty());
    }

    // Through the CPL and BOM import, Create Missing Parts: the board's own parts' packages carry the footprints
    // the board draws; one the library has a package of the same name for takes a copy of it with the board's pads.
    {
        const fs::path dir = fs::temp_directory_path() / "jplacer-test-kicad-board";
        fs::create_directories(dir);
        JPConfiguration config(dir.string());
        auto lib = std::make_shared<JPPackage>();
        lib->id = "LQFP-100_14x14mm_P0.5mm";
        lib->description = "the library's";
        config.addPackage(lib);
        JPCplBomImport imp;
        imp.sources.push_back(top);
        imp.createMissing = true;
        JPBoard b;
        JPCplBomImport::Report report;
        std::string error;
        assert(imp.build(config, "t", b, report, error));
        assert(report.placements == int(top.table.rows.size()) && report.doNotPlace == 1);
        assert(report.footprints == int(top.footprints.size()) && b.find("FID1")->type == JPPlacement::Type::Fiducial);
        const JPBoardPart* u1 = b.part(b.find("U1")->boardPart);
        assert(u1 && u1->state == JPBoardPart::State::Local && u1->localPackage);
        assert(u1->localPackage->id == "LQFP-100_14x14mm_P0.5mm" && u1->localPackage->description == "the library's");
        assert(u1->localPackage->uuid.empty() && u1->localPackage->footprint.pads.size() == 100);
        assert(lib->footprint.pads.empty());   // the library's left as it was
        const JPBoardPart* r1 = b.part(b.find("R1")->boardPart);
        assert(r1 && r1->localPackage && r1->localPackage->footprint.pads.size() == 2);
        // Placed as the .pos of the same board would place it.
        assert(near(b.find("U1")->location.x(), 60.9) && near(b.find("U1")->location.y(), 62.87));
    }
    return 0;
}
