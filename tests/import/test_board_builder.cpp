// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// A board made from its sources: the pick-and-place file in the one frame
// (KiCad's negated bottom X turned back, the origin moved), joined by
// designator to its BOM: empty fields filled, disagreements listed and
// settled the BOM's way only when chosen, designators on one side only
// listed; a designator twice refused.
// Tests check with assert(); a Release build must not compile it away.
#undef NDEBUG
#include <cassert>

#include "import/JPBoardBuilder.h"
#include "import/JPBomImporter.h"

#include <cmath>
#include <filesystem>
#include <fstream>

#include <unistd.h>

using namespace jf;
namespace fs = std::filesystem;

namespace {

std::string write(const fs::path& dir, const char* name, const std::string& text) {
    const fs::path p = dir / name;
    std::ofstream(p, std::ios::binary) << text;
    return p.string();
}

bool near(double a, double b) { return std::abs(a - b) < 1e-9; }

} // namespace

int main() {
    const fs::path dir = fs::temp_directory_path() / ("jplacer-test-builder-" + std::to_string(::getpid()));
    fs::create_directories(dir);

    // The BOM alone: designators listed several to a line.
    {
        std::vector<JPBomImporter::Line> lines;
        std::vector<std::string> notes;
        std::string error;
        assert(JPBomImporter::parse("No.,Designator,Quantity,Supplier Part,Footprint,Value,Mounting Style,Add into BOM\n"
                                    "1,\"C1,C2, C3\",3,C1525,C0402,100nF,SMT,yes\n"
                                    "2,J1,1,C2884991,HDR,,DIP,yes\n"
                                    "3,,0,,,,,\n",
                                    lines, notes, error));
        assert(lines.size() == 2 && lines[0].designators.size() == 3 && lines[0].designators[2] == "C3");
        assert(lines[0].part.value == "100nF" && lines[0].part.supplierNumbers[0].number == "C1525");
        assert(lines[0].part.mounting == JPPlacement::Mounting::Smd && lines[1].part.mounting == JPPlacement::Mounting::ThroughHole);
        assert(notes.size() == 1);   // the line with no designator
    }

    const std::string cpl = write(dir, "cpl.csv",
        "Designator,Footprint,Mid X,Mid Y,Layer,Rotation,Value\n"
        "R1,R0603,10mm,20mm,T,0,10k\n"
        "R2,R0603,-15mm,20mm,B,90,10k\n"
        "C1,C0402,30mm,5mm,T,0,\n"
        "U1,SOIC-8,40mm,40mm,T,0,LM358\n"
        "FID1,FIDUCIAL_1MM,1mm,1mm,T,0,\n");
    const std::string bom = write(dir, "bom.csv",
        "Designator,Value,Manufacturer Part,Supplier Part,Add into BOM\n"
        "\"R1,R2\",10k,0603WAF1002T5E,C25804,yes\n"
        "C1,100nF,CL05B104KO5NNNC,C1525,yes\n"
        "U1,LM358DT,LM358DT,C7950,no\n"
        "D9,LED,,C2286,yes\n");

    // KiCad with negated bottom X, the origin moved, with the BOM.
    {
        const std::vector<JPSource> sources = { { JPSource::Kind::Placements, cpl, JPCadTool::Kind::KiCadNegativeX, "" },
                                                { JPSource::Kind::Bom, bom, JPCadTool::Kind::Other, "" } };
        JPBoardFrame frame;
        frame.originX = 1;
        frame.originY = 2;
        JPBoardBuilder::Result r;
        std::string error;
        assert(JPBoardBuilder::build(sources, frame, {}, r, error));
        const JPPlacement* r1 = r.board.find("R1");
        const JPPlacement* r2 = r.board.find("R2");
        assert(near(r1->x, 9) && near(r1->y, 18));
        assert(near(r2->x, 14) && near(r2->y, 18));            // bottom: -(-15) - 1
        assert(r1->mpn == "0603WAF1002T5E" && r1->supplierNumbers[0].number == "C25804");   // filled from the BOM
        assert(r.board.find("C1")->value == "100nF");                                    // the file left it empty
        // U1: value differs, and the BOM says not placed.
        assert(r.disagreements.size() == 2);
        const JPPlacement* u1 = r.board.find("U1");
        assert(u1->value == "LM358" && !u1->doNotPlace);       // the file's stands until chosen
        assert(r.bomOnly.size() == 1 && r.bomOnly[0] == "D9");
        assert(r.noBomLine.empty());                            // the fiducial needs none

        std::set<std::string> take;
        for (const auto& d : r.disagreements) take.insert(d.key());
        JPBoardBuilder::Result again;
        assert(JPBoardBuilder::build(sources, frame, take, again, error));
        assert(again.board.find("U1")->value == "LM358DT" && again.board.find("U1")->doNotPlace);
        assert(again.disagreements.size() == 2 && again.disagreements[0].takeBom);
    }
    // KiCad as written by default: bottom X as it is. No BOM: nothing joined.
    {
        JPBoardBuilder::Result r;
        std::string error;
        assert(JPBoardBuilder::build({ { JPSource::Kind::Placements, cpl, JPCadTool::Kind::KiCad, "" } }, {}, {}, r, error));
        assert(near(r.board.find("R2")->x, -15) && r.board.find("R1")->mpn.empty() && r.disagreements.empty());
    }
    // A designator twice: refused, and named.
    {
        const std::string dup = write(dir, "dup.csv", "Designator,Mid X,Mid Y\nR1,1,1\nR1,2,2\nR2,3,3\n");
        JPBoardBuilder::Result r;
        std::string error;
        assert(!JPBoardBuilder::build({ { JPSource::Kind::Placements, dup, JPCadTool::Kind::Other, "" } }, {}, {}, r, error));
        assert(error.find("R1") != std::string::npos && error.find("R2") == std::string::npos);
    }
    // A fingerprint changes with the file.
    {
        const std::string a = JPSource::fingerprintOf(cpl);
        assert(!a.empty() && a == JPSource::fingerprintOf(cpl));
        write(dir, "cpl.csv", "Designator,Mid X,Mid Y\nR1,1,1\n");
        assert(JPSource::fingerprintOf(cpl) != a && JPSource::fingerprintOf((dir / "none.csv").string()).empty());
    }
    fs::remove_all(dir);
    return 0;
}
