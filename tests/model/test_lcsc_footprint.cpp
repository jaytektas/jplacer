// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// A part from EasyEDA's component lookup by LCSC part number (JPEasyEdaPart, JPLcscLookup), from an answer made up
// here in its form: its top pads in millimetres from the footprint's origin, Y up (a rectangle, an oval turned, a
// polygon made the rectangle round it), a through-hole pad left out, pad 1 marked, the height its 3D model's name
// gives; LCSC part numbers as typed made LCSC's; an answer kept found again with no network; a package given the
// pads in its own units; a part given what it lacks only; a part's LCSC number read from its supplier part number.
// Tests check with assert(); a Release build must not compile it away.
#undef NDEBUG
#include <cassert>

#include "model/JPEasyEdaPart.h"
#include "model/JPLcscLookup.h"

#include <cmath>
#include <filesystem>
#include <fstream>

using namespace jf;

namespace {

bool near(double a, double b) { return std::abs(a - b) < 1e-6; }

// Its origin at (4000, 3000); units of 10 mil (0.254 mm).
const char* kAnswer = R"({
  "success": true,
  "result": {
    "title": "TEST-PART-1",
    "description": "A test part",
    "lcsc": { "number": "C99999" },
    "dataStr": { "head": { "c_para": { "Manufacturer": "Maker", "Manufacturer Part": "MPN-1", "Supplier Part": "C99999" } } },
    "packageDetail": {
      "title": "TEST-3_L2.0-W1.0",
      "dataStr": {
        "head": { "x": 4000, "y": 3000, "c_para": { "3DModel": "TEST-3_L2.0-W1.0-H0.55" } },
        "shape": [
          "TRACK~1~3~~3990 2990 4010 2990~gge1~0",
          "PAD~RECT~3995~3000~4~2~1~~1~0~~0~gge2~0~~Y~0",
          "PAD~OVAL~4005~3010~2~6~1~~2~0~~90~gge3~0~~Y~0",
          "PAD~POLYGON~4000~2990~0~0~1~~3~0~3998 2988 4002 2988 4002 2994 3998 2994~0~gge4~0~~Y~0",
          "PAD~ELLIPSE~4020~3000~6~6~11~~4~1.5~~0~gge5~0~~Y~0"
        ]
      }
    }
  }
})";

} // namespace

int main() {
    // Part numbers as typed.
    assert(JPEasyEdaPart::partNumber(" c14663 ") == "C14663" && JPEasyEdaPart::partNumber("14663") == "C14663");
    assert(JPEasyEdaPart::partNumber("CL10B104") == "" && JPEasyEdaPart::partNumber("") == "");

    JPEasyEdaPart p;
    std::string why;
    assert(JPEasyEdaPart::parse(kAnswer, p, why));
    assert(p.lcsc == "C99999" && p.mpn == "MPN-1" && p.manufacturer == "Maker" && p.description == "A test part");
    assert(p.footprintName == "TEST-3_L2.0-W1.0" && p.heightMm && near(*p.heightMm, 0.55));
    assert(p.pads.size() == 3 && p.throughHole == 1 && p.polygons == 1);
    // The rectangle: left of the origin, pad 1 marked.
    const JPFootprint::Pad& a = p.pads[0];
    assert(a.name == "1" && a.mark && near(a.x, -1.27) && near(a.y, 0) && near(a.width, 1.016) && near(a.height, 0.508));
    assert(a.roundness == 0 && a.rotation == 0);
    // The oval: right and below (Y up: negative), round, turned as EasyEDA turns it.
    const JPFootprint::Pad& b = p.pads[1];
    assert(near(b.x, 1.27) && near(b.y, -2.54) && b.roundness == 100 && near(b.rotation, 90) && !b.mark);
    // The polygon: the rectangle round its points, above the origin.
    const JPFootprint::Pad& c = p.pads[2];
    assert(near(c.x, 0) && near(c.y, 2.286) && near(c.width, 1.016) && near(c.height, 1.524));
    // No such part; a footprint of only through-hole pads.
    assert(!JPEasyEdaPart::parse(R"({"success":false,"result":{}})", p, why) && why.find("no such part") != std::string::npos);
    assert(!JPEasyEdaPart::parse("not json", p, why));

    // Kept: found again with no network.
    namespace fs = std::filesystem;
    const fs::path cache = fs::temp_directory_path() / "jplacer-test-lcsc";
    fs::remove_all(cache);
    fs::create_directories(cache);
    std::ofstream(cache / "C99999.json") << kAnswer;
    JPEasyEdaPart kept;
    assert(JPLcscLookup::find(cache.string(), "c99999", kept, why) && kept.pads.size() == 3);
    assert(!JPLcscLookup::find(cache.string(), "R1", kept, why) && why.find("not an LCSC part number") != std::string::npos);

    // A package in inches given the pads, in inches.
    JPPackage pkg;
    pkg.footprint.units = JPLengthUnit::Inches;
    pkg.footprint.pads.push_back({});
    JPLcscLookup::giveFootprint(pkg, kept);
    assert(pkg.footprint.pads.size() == 3 && near(pkg.footprint.pads[0].x, -0.05) && pkg.footprint.units == JPLengthUnit::Inches);

    // A part given what it lacks, and only that.
    JPPart part;
    part.identifiers.push_back({ "supplierPn", "LCSC", "C99999" });
    assert(JPLcscLookup::lcscOf(part) == "C99999");
    JPLcscLookup::describe(part, kept);
    assert(part.name == std::optional<std::string>("A test part") && near(part.height.value(), 0.55));
    assert(part.identifiers.size() == 2 && part.identifiers[1].kind == "mpn" && part.identifiers[1].code == "MPN-1");
    JPPart mine;
    mine.name = "Mine";
    mine.height = JPLength(1.2, JPLengthUnit::Millimeters);
    JPLcscLookup::describe(mine, kept);
    assert(*mine.name == "Mine" && near(mine.height.value(), 1.2));
    // A supplier part number of another supplier's: none.
    JPPart other;
    other.identifiers.push_back({ "supplierPn", "Digikey", "311-1344-1-ND" });
    assert(JPLcscLookup::lcscOf(other).empty());
    fs::remove_all(cache);
    return 0;
}
