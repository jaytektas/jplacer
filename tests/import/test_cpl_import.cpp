// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// Pick-and-place files as different PCB tools write them: columns found by
// their headings, units in the numbers or the heading, comma or semicolon,
// a byte-order mark, quoted fields; the part's centre preferred to its
// reference point; fiducials told by name or footprint; bad rows noted.
// Tests check with assert(); a Release build must not compile it away.
#undef NDEBUG
#include <cassert>

#include "import/JPCplImporter.h"

#include <cmath>

using namespace jf;

int main() {
    // EasyEDA Pro / JLCPCB style: centre and reference columns, units in the numbers, T/B.
    {
        const std::string csv = "\xEF\xBB\xBF\"Designator\",\"Device\",\"Footprint\",\"Mid X\",\"Mid Y\",\"Ref X\",\"Ref Y\","
                                "\"Pad X\",\"Pad Y\",\"Pins\",\"Layer\",\"Rotation\",\"SMD\",\"Comment\"\r\n"
                                "\"FID1\",\"FIDUCIAL\",\"FIDUCIAL_1MM\",\"2.383mm\",\"9.535mm\",\"2.383mm\",\"9.535mm\",\"2.383mm\",\"9.535mm\",\"1\",\"T\",\"0\",\"Yes\",\"FIDUCIAL\"\r\n"
                                "\"R1\",\"R\",\"R0402\",\"40.5mm\",\"80.25mm\",\"40.0mm\",\"80.0mm\",\"39.5mm\",\"80.25mm\",\"2\",\"B\",\"270\",\"Yes\",\"10k\"\r\n"
                                "\"C7\",\"C\",\"C0603\",\"1000mil\",\"500mil\",\"0mil\",\"0mil\",\"0mil\",\"0mil\",\"2\",\"T\",\"45\",\"Yes\",\"100nF\"\r\n";
        JPBoard b;
        std::vector<std::string> notes;
        std::string error;
        assert(JPCplImporter::parse(csv, b, notes, error) && notes.empty());
        assert(b.placements.size() == 3);
        const JPPlacement* fid = b.find("FID1");
        assert(fid && fid->fiducial && fid->side == JPPlacement::Side::Top && std::abs(fid->x - 2.383) < 1e-9);
        assert(fid->fiducialMm == 1.0 && b.find("R1")->fiducialMm == 0);   // FIDUCIAL_1MM; a part has none
        const JPPlacement* r1 = b.find("R1");
        assert(r1 && !r1->fiducial && r1->side == JPPlacement::Side::Bottom);
        assert(std::abs(r1->x - 40.5) < 1e-9 && std::abs(r1->y - 80.25) < 1e-9);   // Mid, not Ref or Pad
        assert(std::abs(r1->rotationDeg - 270) < 1e-9 && r1->footprint == "R0402" && r1->value == "10k");
        const JPPlacement* c7 = b.find("C7");
        assert(c7 && std::abs(c7->x - 25.4) < 1e-9 && std::abs(c7->y - 12.7) < 1e-9);   // mil
        assert(b.fiducials(JPPlacement::Side::Top).size() == 1 && b.fiducials(JPPlacement::Side::Bottom).empty());
    }
    // KiCad .pos as CSV: Ref, Val, Package, PosX, PosY, Rot, Side (top/bottom).
    {
        const std::string csv = "Ref,Val,Package,PosX,PosY,Rot,Side\n"
                                "\"FID2\",\"Fiducial\",\"Fiducial_0.75mm_Mask1.5mm\",150.0,-80.5,0,bottom\n"
                                "\"U1\",\"STM32\",\"LQFP-64\",120.25,-60.0,90.0,top\n";
        JPBoard b;
        std::vector<std::string> notes;
        std::string error;
        assert(JPCplImporter::parse(csv, b, notes, error) && b.placements.size() == 2);
        assert(b.fiducials(JPPlacement::Side::Bottom).size() == 1);
        assert(b.fiducials(JPPlacement::Side::Bottom).front()->fiducialMm == 0.75);   // the copper, not the mask
        const JPPlacement* u1 = b.find("U1");
        assert(u1 && u1->value == "STM32" && u1->footprint == "LQFP-64" && std::abs(u1->y + 60) < 1e-9);
    }
    // Semicolons, units in the heading, no side or rotation column, a bad row.
    {
        const std::string csv = "# exported\nDesignator;Center-X(mil);Center-Y(mil);Package\n"
                                "D1;100;200;LED0603\n"
                                "D2;;;LED0603\n";
        JPBoard b;
        std::vector<std::string> notes;
        std::string error;
        assert(JPCplImporter::parse(csv, b, notes, error) && b.placements.size() == 1);
        assert(std::abs(b.placements[0].x - 2.54) < 1e-9 && std::abs(b.placements[0].y - 5.08) < 1e-9);
        assert(notes.size() == 3);   // no side, no rotation, one row left out
    }
    // Not a placement file.
    {
        JPBoard b;
        std::vector<std::string> notes;
        std::string error;
        assert(!JPCplImporter::parse("Name,Quantity\nR1,3\n", b, notes, error) && !error.empty());
    }
    return 0;
}
