// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// OpenPnP's board importers, each on a file of its format: the placements,
// sides and rotations read as OpenPnP's read them, the parts and packages
// made, and the failures OpenPnP reports.
// Tests check with assert(); a Release build must not compile it away.
#undef NDEBUG
#include <cassert>

#include "model/JPAltiumCsvImporter.h"
#include "model/JPDipTraceImporter.h"
#include "model/JPEagleBoardImporter.h"
#include "model/JPEagleMountsmdUlpImporter.h"
#include "model/JPKicadPosImporter.h"
#include "model/JPLabcenterProteusImporter.h"
#include "model/JPReferenceCsvImporter.h"

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>

using namespace jf;
namespace fs = std::filesystem;

namespace {

bool near(double a, double b) { return std::fabs(a - b) < 1e-9; }

fs::path g_dir;

std::string write(const std::string& name, const std::string& text) {
    const fs::path p = g_dir / name;
    std::ofstream(p, std::ios::binary) << text;
    return p.string();
}

JPBoard importWith(const JPBoardImporter& importer, const std::vector<std::string>& files,
                   const std::vector<bool>& options, JPConfiguration& config) {
    JPBoard board;
    std::string error;
    const bool ok = importer.read(files, options, config, board, error);
    if (!ok) std::fprintf(stderr, "%s: %s\n", importer.name().c_str(), error.c_str());
    assert(ok);
    return board;
}

std::vector<bool> initial(const JPBoardImporter& importer) {
    std::vector<bool> v;
    for (const auto& o : importer.options()) v.push_back(o.initial);
    return v;
}

} // namespace

int main() {
    g_dir = fs::temp_directory_path() / "jplacer-test-board-importers";
    fs::remove_all(g_dir);
    fs::create_directories(g_dir);

    // The menu, in OpenPnP's order.
    {
        const auto all = JPBoardImporter::all();
        std::vector<std::string> names;
        for (const auto& i : all) names.push_back(i->name());
        assert((names == std::vector<std::string> { "Altium .csv", "Diptrace .csv", "CadSoft EAGLE Board",
                                                    "EAGLE mountsmd.ulp", "KiCAD .pos", "Labcenter Proteus .pkp",
                                                    "Reference CSV" }));
    }

    // KiCad: a bottom part's X turned over and its rotation 180 less it;
    // parts "Package-Value", the board's own (DESIGN.md, Board part): the
    // library's where it has one, else made on the board when asked, else
    // kept unmatched; the library itself never added to.
    {
        JPConfiguration config(g_dir.string());
        const std::string top = write("t.pos", "### Module positions ###\n## Unit = mm, Angle = deg.\n"
                                               "# Ref Val Package PosX PosY Rot Side\n"
                                               "C1 100n C_0603 128.9050 -52.0700 0.0 top\n"
                                               "R1 10k R_0603 10.0000 20.0000 90.0000 top\n");
        const std::string bottom = write("b.pos", "U1 LM358 SOIC-8 -30.5000 12.2500 45.0000 bottom\r\n"
                                                  "U2 LM358 SOIC-8 -40.0000 1.0000 180.0000 bottom\r\n");
        JPKicadPosImporter k;
        // Not made: each placement's board part unmatched, named as the file names it.
        JPBoard b = importWith(k, { top, bottom }, initial(k), config);
        assert(b.placements.size() == 4 && b.placements[0].partId == "C_0603-100n" && config.parts().empty());
        assert(b.parts().size() == 3 && b.part(b.placements[0].boardPart)->state == JPBoardPart::State::Unmatched);
        assert(b.part(b.placements[0].boardPart)->field("value") == "100n"
               && b.part(b.placements[0].boardPart)->field("footprint") == "C_0603");
        assert(b.placements[2].id == "U1" && b.placements[2].side == JPSide::Bottom);
        assert(near(b.placements[2].location.x(), 30.5) && near(b.placements[2].location.rotation(), 135));
        assert(b.placements[3].location.rotation() == 0.0 && !std::signbit(b.placements[3].location.rotation()));
        // Made: the board's own SOIC-8-LM358, once, with a package of its own; nothing in the library.
        b = importWith(k, { top, bottom }, { true, true, false }, config);
        assert(b.placements[2].partId == "SOIC-8-LM358" && b.placements[3].boardPart == b.placements[2].boardPart);
        const JPBoardPart* soic = b.part(b.placements[2].boardPart);
        assert(soic->state == JPBoardPart::State::Local && soic->localPart->packageId == "SOIC-8"
               && soic->localPackage && soic->localPackage->id == "SOIC-8");
        assert(config.parts().empty() && config.packages().empty());
        // One the library has: matched to it, the library as it was.
        {
            auto lm = std::make_shared<JPPart>();
            lm->id = "SOIC-8-LM358";
            config.addPart(lm);
            b = importWith(k, { top, bottom }, { true, true, false }, config);
            const JPBoardPart* m = b.part(b.placements[2].boardPart);
            assert(m->state == JPBoardPart::State::Matched && m->libraryPartId == "SOIC-8-LM358" && config.parts().size() == 1);
            config.removePart("SOIC-8-LM358");
        }
        // The value alone.
        b = importWith(k, { top, "" }, { true, true, true }, config);
        assert(b.placements.size() == 2 && b.placements[1].partId == "10k");
        // A file in inches (KiCad 9 writes "## Unit = inches"): its positions in millimetres, 25.4 to the inch.
        const std::string inches = write("in.pos", "### Footprint positions - created on 2026-10-08T15:11:44+1100 ###\n"
                                                   "### Printed by KiCad version 9.0.8\n## Unit = inches, Angle = deg.\n"
                                                   "## Side : All\n# Ref Val Package PosX PosY Rot Side\n"
                                                   "C1 10pF C_0603_1608Metric 1.7291 2.5463 -90.0000 top\n");
        b = importWith(k, { inches, "" }, initial(k), config);
        assert(b.placements.size() == 1 && near(b.placements[0].location.x(), 1.7291 * 25.4)
               && near(b.placements[0].location.y(), 2.5463 * 25.4)
               && b.placements[0].location.units() == JPLengthUnit::Millimeters);
        // A line not of the format.
        JPBoard bad;
        std::string error;
        assert(!k.read({ write("bad.pos", "C1 100n\n"), "" }, initial(k), config, bad, error) && error == "No match found");
    }

    // Reference CSV: the header found below other lines, in mils, sides by
    // B/Y, fiducials by name, rotation within ±180, heights made and updated.
    {
        JPConfiguration config(g_dir.string());
        const std::string csv = write("r.csv", "Made by some tool\n\n"
                                               "Designator,Comment,Footprint,Ref-X(mil),Ref-Y(mil),Rotation,Layer,Height(mil)\n"
                                               "C1,100n,C0603,1000,2000,270,TopLayer,20\n"
                                               "FID1,,FID1MM,100,100,0,TopLayer,0\n"
                                               "\"R1, R2\",1k,R0603,\"50\",50,-190,BottomLayer,10\n"
                                               "short,line\n");
        JPReferenceCsvImporter r;
        JPBoard b = importWith(r, { csv }, initial(r), config);
        assert(b.placements.size() == 3);
        assert(near(b.placements[0].location.x(), 25.4) && near(b.placements[0].location.y(), 50.8));
        assert(near(b.placements[0].location.rotation(), -90) && b.placements[0].side == JPSide::Top);
        assert(b.placements[1].type == JPPlacement::Type::Fiducial && b.placements[1].partId == "FID1MM-");
        assert(b.placements[2].id == "R1, R2" && b.placements[2].side == JPSide::Bottom);
        assert(near(b.placements[2].location.rotation(), 170));
        // The height given to the board's own part made for it; the library left alone.
        assert(near(b.part(b.placements[0].boardPart)->localPart->height.value(), 0.508) && config.parts().empty());
        // Not made: kept, their parts unmatched (nothing the file says is thrown away).
        JPConfiguration empty(g_dir.string());
        b = importWith(r, { csv }, { false, false }, empty);
        assert(b.placements.size() == 3 && b.part(b.placements[0].boardPart)->state == JPBoardPart::State::Unmatched);
        // No header.
        JPBoard bad;
        std::string error;
        assert(!r.read({ write("n.csv", "a,b,c\n1,2,3\n") }, initial(r), config, bad, error));
        assert(error.rfind("Unable to find relevant headers' names.", 0) == 0);
    }

    // Altium: tab separated, UTF-16 with its mark, a comment column.
    {
        JPConfiguration config(g_dir.string());
        const std::string text = "Designator\tComment\tLayer\tFootprint\tCenter-X(mm)\tCenter-Y(mm)\tRotation\tDescription\r\n"
                                 "U1\tSTM32\tBottomLayer\tLQFP48\t12.5\t7.25\t90\tMCU\r\n";
        std::string utf16 = "\xFF\xFE";
        for (char c : text) {
            utf16 += c;
            utf16 += '\0';
        }
        JPAltiumCsvImporter a;
        JPBoard b = importWith(a, { write("a.csv", utf16) }, initial(a), config);
        assert(b.placements.size() == 1 && b.placements[0].partId == "LQFP48-STM32" && *b.placements[0].comments == "MCU");
        assert(b.placements[0].side == JPSide::Bottom && near(b.placements[0].location.x(), 12.5));
    }

    // DipTrace: the first line passed over; Top/Bottom by its first letter.
    {
        JPConfiguration config(g_dir.string());
        JPDipTraceImporter d;
        JPBoard b = importWith(d, { write("d.csv", "RefDes,Name,X (mm),Y (mm),Side,Rotate,Value\n"
                                                   "C1,C0603,8.6,7.2,Top,0,1nF\nC2,C0402,10.81,22.99,Bottom,180,0.1uF/16V\n") },
                               initial(d), config);
        assert(b.placements.size() == 2 && b.placements[1].side == JPSide::Bottom && b.placements[1].partId == "C0402-0.1uF/16V");
        JPBoard bad;
        std::string error;
        assert(!d.read({ write("d2.csv", "h\nC1,C0603,8.6\n") }, initial(d), config, bad, error));
        assert(error == "Index 6 out of bounds for length 3");
    }

    // Proteus: thou, with stock codes.
    {
        JPConfiguration config(g_dir.string());
        JPLabcenterProteusImporter p;
        JPBoard b = importWith(p, { write("p.pkp", "Proteus PCB Design\nUnits used = thou\n\n"
                                                   "\"R1\",\"10k\",\"0402\",\"RS123\",TOP,270,1000,500\n"
                                                   "\"R2\",\"10k\",\"0402\",\"RS123\",BOTTOM,-180,100,50\n") },
                               { true, true }, config);
        assert(b.placements.size() == 2 && b.placements[0].id == "R1" && b.placements[0].partId == "0402-10k");
        assert(near(b.placements[0].location.x(), 25.4) && near(b.placements[0].location.y(), 12.7));
        assert(near(b.placements[0].location.rotation(), 270) && b.placements[1].side == JPSide::Bottom);
    }

    // EAGLE mountsmd: top and bottom files; the value taken as the package
    // when there is none.
    {
        JPConfiguration config(g_dir.string());
        JPEagleMountsmdUlpImporter m;
        JPBoard b = importWith(m, { write("m.mnt", "C1 41.91 34.93 180 0.1uF C0805\nRULER 87.00 49.00 0 RULER\n"),
                                    write("m.mnb", "T10 21.59 14.22 90 SOT23-BEC\n") },
                               initial(m), config);
        assert(b.placements.size() == 3 && b.placements[0].partId == "C0805-0.1uF" && b.placements[1].partId == "RULER");
        assert(b.placements[2].side == JPSide::Bottom && b.placements[2].partId == "SOT23-BEC");
    }

    // EAGLE board: elements, packages with the SMD pads as footprints,
    // mirrored ones on the bottom, and solder paste pads.
    {
        JPConfiguration config(g_dir.string());
        const std::string brd = write("e.brd", R"(<?xml version="1.0" encoding="utf-8"?>
<!DOCTYPE eagle SYSTEM "eagle.dtd">
<eagle version="9.6.2">
<drawing>
<layers>
<layer number="1" name="Top"/><layer number="16" name="Bottom"/><layer number="20" name="Dimension"/>
<layer number="31" name="tCream"/><layer number="32" name="bCream"/>
</layers>
<board>
<plain>
<wire x1="0" y1="0" x2="50" y2="0" width="0" layer="20"/>
<wire x1="50" y1="0" x2="50" y2="30" width="0" layer="20"/>
</plain>
<libraries>
<library name="rcl">
<packages>
<package name="R0603">
<smd name="1" x="-0.85" y="0" dx="1" dy="1.1" layer="1"/>
<smd name="2" x="0.85" y="0" dx="1" dy="1.1" layer="1" rot="R90" roundness="20"/>
<wire x1="0" y1="0" x2="1" y2="1" width="0.1" layer="21"/>
</package>
</packages>
</library>
</libraries>
<designrules name="default">
<param name="mlMinCreamFrame" value="0mil"/>
<param name="mlMaxCreamFrame" value="0mil"/>
</designrules>
<elements>
<element name="R1" library="rcl" package="R0603" value="10k" x="10" y="5"/>
<element name="R2" library="rcl" package="R0603" value="10k" x="20" y="5" rot="MR90"/>
</elements>
</board>
</drawing>
</eagle>
)");
        JPEagleBoardImporter e;
        JPBoard b = importWith(e, { brd }, initial(e), config);
        assert(b.placements.size() == 2 && b.placements[0].partId == "R0603-10k" && b.placements[1].side == JPSide::Bottom);
        assert(near(b.placements[1].location.rotation(), 90));
        // The footprint the board's library draws, on the board's own package (the library not added to).
        const JPPackage* k = b.part(b.placements[0].boardPart)->localPackage.get();
        assert(config.packages().empty() && k && k->id == "R0603");
        assert(k->footprint.pads.size() == 2 && near(k->footprint.pads[1].rotation, 90) && near(k->footprint.pads[1].roundness, 20));
        assert(b.solderPastePads.size() == 4 && *b.solderPastePads[0].name == "R1-1");
        assert(near(b.solderPastePads[0].location.x(), 9.15) && near(b.solderPastePads[0].location.y(), 5));
        assert(near(b.solderPastePads[0].pad.height, 1) && near(b.solderPastePads[0].pad.width, 1.1));
        assert(b.solderPastePads[2].side == JPSide::Bottom);
        // Only the top.
        b = importWith(e, { brd }, { true, false, true, true, false }, config);
        assert(b.placements.size() == 1 && b.placements[0].partId == "rcl-R0603-10k");
    }

    fs::remove_all(g_dir);
    return 0;
}
