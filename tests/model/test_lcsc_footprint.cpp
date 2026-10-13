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

#include "model/JPConfiguration.h"
#include "model/JPEasyEdaModel.h"
#include "model/JPEasyEdaPart.h"
#include "model/JPLcscFetch.h"
#include "openpnp/JPXmlJson.h"
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
          "PAD~ELLIPSE~4020~3000~6~6~11~~4~1.5~~0~gge5~0~~Y~0",
          "SVGNODE~{\"gId\":\"g1_outline\",\"attrs\":{\"c_width\":\"7.87401575\",\"c_height\":\"3.93700787\",\"c_rotation\":\"90,0,0\",\"z\":\"0\",\"uuid\":\"abc123\",\"c_etype\":\"outline3D\"}}"
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
    // Its 3D model: its id, the box it covers (2.0 x 1.0 mm), how it is turned.
    assert(p.model && p.model->uuid == "abc123" && near(p.model->widthMm, 2.0) && near(p.model->lengthMm, 1.0));
    assert(near(p.model->rotationX, 90) && near(p.model->rotationY, 0));
    // A model turned a quarter about Z: the box round its outline as placed (1.6 across X, 0.8 along Y), not its
    // own c_width and c_height (0.8 by 1.6); with no outline, those swapped.
    {
        std::string turned = kAnswer;
        const size_t at = turned.find("SVGNODE~");
        assert(at != std::string::npos);
        const size_t end = turned.find("\"\n", at);
        turned.replace(at, end - at,
                       "SVGNODE~{\\\"attrs\\\":{\\\"c_width\\\":\\\"3.14960630\\\",\\\"c_height\\\":\\\"6.29921260\\\","
                       "\\\"c_rotation\\\":\\\"0,0,90\\\",\\\"uuid\\\":\\\"r1\\\",\\\"c_etype\\\":\\\"outline3D\\\"},"
                       "\\\"childNodes\\\":[{\\\"attrs\\\":{\\\"points\\\":\\\"3996.8503937 2998.4251969 4003.1496063 "
                       "2998.4251969 4003.1496063 3001.5748031\\\"}}]}");
        JPEasyEdaPart r;
        assert(JPEasyEdaPart::parse(turned, r, why) && r.model && r.model->uuid == "r1");
        assert(near(r.model->widthMm, 1.6) && near(r.model->lengthMm, 0.8) && near(r.model->rotationZ, 90));
    }
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

    // A model's height: across its vertices' Z; turned 90 degrees about X, across their Y.
    const std::string obj = "# box\nv -1 -0.5 -0.25\nv 1 0.5 0.25\nvn 0 0 1\nf 1 2 1\n";
    double h = 0;
    assert(JPEasyEdaModel::height(obj, 0, 0, h, why) && near(h, 0.5));
    assert(JPEasyEdaModel::height(obj, 90, 0, h, why) && near(h, 1.0));
    assert(!JPEasyEdaModel::height("# nothing\n", 0, 0, h, why));

    // A package in inches given its body and height where it has none; what it has kept.
    JPPackage body;
    body.footprint.units = JPLengthUnit::Inches;
    assert(JPLcscLookup::giveDimensions(body, kept, 0.55));
    assert(near(body.footprint.bodyWidth, 2.0 / 25.4) && near(body.footprint.bodyHeight, 1.0 / 25.4) && near(body.height.value(), 0.55));
    JPPackage sized;
    sized.footprint.bodyWidth = 3;
    sized.height = JPLength(2, JPLengthUnit::Millimeters);
    assert(!JPLcscLookup::giveDimensions(sized, kept, 0.55) && near(sized.footprint.bodyWidth, 3) && sized.footprint.bodyHeight == 0);

    // The fetch, from what is kept (no network): the part and its model; asked for a package with no pads, body
    // or height whose part has the LCSC part number, given all three; a package with nothing missing not asked for.
    fs::create_directories(cache / JPLcscLookup::kModelFolder);
    std::ofstream(cache / JPLcscLookup::kModelFolder / "abc123.obj") << obj;
    JPConfiguration config(cache.string());
    auto pk = std::make_shared<JPPackage>();
    pk->id = "pkg-1";
    pk->label = "TEST-3";
    config.addPackage(pk);
    auto done = std::make_shared<JPPackage>();
    done->id = "pkg-2";
    done->label = "DONE";
    done->footprint.pads.push_back({});
    done->footprint.bodyWidth = 1;
    done->height = JPLength(1, JPLengthUnit::Millimeters);
    config.addPackage(done);
    auto pt = std::make_shared<JPPart>();
    pt->id = "part-1";
    pt->packageId = "pkg-1";
    pt->identifiers.push_back({ "supplierPn", "LCSC", "C99999" });
    config.addPart(pt);
    int without = -1;
    const std::vector<JPLcscFetch::Ask> asks = JPLcscFetch::asksFor(config, {}, true, true, without);
    assert(asks.size() == 1 && without == 0 && asks[0].packageId == "pkg-1" && asks[0].lcsc == "C99999" && asks[0].pads && asks[0].dimensions);
    const JPLcscFetch::Answer answer = JPLcscFetch::fetch(cache.string(), asks[0]);
    assert(answer.found && answer.problems.empty() && answer.heightMm && near(*answer.heightMm, 1.0));
    assert(JPLcscFetch::give(config, asks[0], answer) == "3 pads, body, height");
    assert(pk->footprint.pads.size() == 3 && near(pk->footprint.bodyWidth, 2.0) && near(pk->height.value(), 1.0));
    // Where each came from, the part and its model found kept; carried in the package as kept, and read back.
    assert(answer.partKept && answer.modelKept);
    assert(pk->padsSource == "LCSC C99999 (EasyEDA)" && pk->bodySource == "LCSC C99999's 3D model (EasyEDA) abc123");
    assert(pk->heightSource == pk->bodySource);
    const JPPackage back = JPPackage::fromXml(JPXmlJson::element("package", JPXmlJson::from(pk->toXml())));
    assert(back.padsSource == pk->padsSource && back.bodySource == pk->bodySource && back.heightSource == pk->heightSource);
    assert(JPPackage::fromXml(JPXmlJson::element("package", JPXmlJson::from(done->toXml()))).padsSource.empty());
    assert(JPLcscFetch::asksFor(config, {}, true, true, without).empty());
    // A model not there to be had: said, the rest given.
    fs::remove(cache / JPLcscLookup::kModelFolder / "abc123.obj");
    JPEasyEdaPart noModel = kept;
    noModel.model.reset();
    assert(!JPLcscLookup::modelHeight(cache.string(), noModel, h, why) && why.find("no 3D model") != std::string::npos);
    fs::remove_all(cache);
    return 0;
}
