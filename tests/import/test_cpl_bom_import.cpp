// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// Importing a board from its CAD files (DESIGN.md, Import): tables read whatever their separator and
// encoding, their header found (KiCad's "# Ref Val …" too); columns guessed from the names tools give them;
// lengths in the units a header or cell says; designator lists expanded; a CPL and a BOM joined, what does
// not fit reported (in one file only, fields they disagree on, the file chosen to win); parts grouped by
// MPN, else value and footprint, every field kept, matched to the library or not; do-not-place; the
// supplier a "LCSC Part #" column names; mapping profiles found and applied; the files kept with the board.
// Tests check with assert(); a Release build must not compile it away.
#undef NDEBUG
#include <cassert>
#include <cmath>

#include "import/JPCplBomImport.h"
#include "import/JPDesignators.h"
#include "import/JPMappingProfiles.h"

#include <filesystem>

using namespace jf;
using I = JPImportField::Id;

namespace {

JPImportSource source(const std::string& text, JPImportSource::Role role, const std::string& name) {
    JPImportSource s;
    s.role = role;
    std::string error;
    assert(JPTableFile::parse(text, s.table, error));
    s.table.path = "/cad/" + name;
    s.guess();
    return s;
}

bool near(double a, double b) { return std::abs(a - b) < 1e-6; }

} // namespace

int main() {
    // Headers as tools write them.
    assert(JPImportField::guess("Ref-X(mm)") == I::X && JPImportField::guess("Mid Y") == I::Y);
    assert(JPImportField::guess("PosX") == I::X && JPImportField::guess("LCSC Part #") == I::SupplierPn);
    assert(JPImportField::guess("Mfr. Part #") == I::Mpn && JPImportField::guess("Manufacturer") == I::Manufacturer);
    assert(JPImportField::guess("Comment") == I::Value && JPImportField::guess("Voltage") == I::Extra);
    assert(JPImportField::guess("Layer") == I::Side && JPImportField::guess("Do Not Place") == I::DoNotPlace);

    // Designators.
    assert((JPDesignators::expand("R1, R2, R5-R8") == std::vector<std::string>{ "R1", "R2", "R5", "R6", "R7", "R8" }));
    assert((JPDesignators::expand("C3 C4;C3") == std::vector<std::string>{ "C3", "C4" }));
    assert((JPDesignators::expand("R5-7") == std::vector<std::string>{ "R5", "R6", "R7" }));
    assert((JPDesignators::expand("U1-A") == std::vector<std::string>{ "U1-A" }));

    // KiCad's .pos: comments, its header behind "#", runs of spaces; "Package" is the footprint.
    {
        const JPImportSource k = source("### Module positions - created on x ###\n## Unit = mm, Angle = deg.\n"
                                        "# Ref     Val       Package                PosX       PosY       Rot  Side\n"
                                        "C1        100n      C_0603_1608Metric   128.9050   -52.0700   0.0  top\n"
                                        "## End\n", JPImportSource::Role::Cpl, "k.pos");
        assert(k.table.separator == ' ' && k.table.rows.size() == 1 && k.table.header.size() == 7);
        assert(k.column(I::Designator) == 0 && k.column(I::Footprint) == 2 && k.column(I::X) == 3 && k.column(I::Side) == 6);
    }
    // Semicolons, a decimal comma, quoted cells with separators in them; mils from the header.
    {
        const JPImportSource s = source("Designator;Ref-X(mil);Ref-Y(mil);Rotation;Layer\n\"R1;x\";1000;\"2000,5\";90;BottomLayer\n",
                                        JPImportSource::Role::Cpl, "s.csv");
        assert(s.table.separator == ';' && s.table.rows[0][0] == "R1;x" && s.units == JPLengthUnit::Mils);
        double mm = 0;
        assert(JPImportSource::length(s.table.rows[0][2], s.units, mm) && near(mm, 50.8127));
        assert(JPImportSource::length("12.5mm", JPLengthUnit::Mils, mm) && near(mm, 12.5));
        assert(!JPImportSource::length("abc", JPLengthUnit::Millimeters, mm));
    }
    // UTF-16 (Altium's), Latin-1 made UTF-8.
    {
        std::string u16 = "\xFF\xFE";
        for (const char c : std::string("Designator\tX\tY\nR1\t1\t2\n")) { u16 += c; u16 += '\0'; }
        JPTableFile t;
        std::string error;
        assert(JPTableFile::parse(JPTableFile::utf8(u16), t, error) && t.separator == '\t' && t.rows[0][0] == "R1");
        assert(JPTableFile::utf8("100\xB5" "F") == "100\xC2\xB5" "F");
    }

    // A CPL (JLCPCB's) and a BOM (JLCPCB's, with a manufacturer the user's file calls "Provider").
    std::vector<JPImportSource> files;
    files.push_back(source("Designator,Mid X,Mid Y,Layer,Rotation\n"
                           "C1,10,5,Top,0\nC2,12,5,Top,90\nR1,20,5,Top,0\nR2,22,5,Top,0\nU1,30,10,Bottom,180\n"
                           "FID1,1,1,Top,0\nJ1,40,10,Top,0\n",
                           JPImportSource::Role::Cpl, "cpl.csv"));
    files.push_back(source("Comment,Designator,Footprint,LCSC Part #,MPN,Provider,Voltage,DNP\n"
                           "100n,\"C1,C2\",C0603,C14663,CL10B104KB8NNNC,Samsung,50V,\n"
                           "10k,R1-R2,R0603,C25804,0603WAF1002T5E,UniOhm,,\n"
                           "LM358,U1,SOIC-8,C7950,LM358DR,TI,,\n"
                           "22u,C9,C0805,C45783,CL21A226MAQNNNE,Samsung,,\n"
                           "NC,J1,Conn,,,,,DNP\n",
                           JPImportSource::Role::Bom, "bom.csv"));
    assert(files[1].column(I::SupplierPn) == 3 && files[1].column(I::Mpn) == 4 && files[1].mapping[5] == I::Extra);
    files[1].mapping[5] = I::Manufacturer;   // the user's choice: "Provider" is the manufacturer

    JPCplBomImport imp;
    imp.sources = files;
    // The join: C9 in the BOM only; FID1 (a fiducial) not counted as missing from it.
    {
        const JPCplBomImport::Report r = imp.check();
        assert((r.otherOnly == std::vector<std::string>{ "C9" }) && r.cplOnly.empty() && r.placements == 7);
    }
    // A field the files disagree on: the BOM's value wins by default; the CPL's when chosen.
    imp.sources[0].table.header.push_back("Val");
    imp.sources[0].mapping.push_back(I::Value);
    for (auto& row : imp.sources[0].table.rows) row.push_back(row[0] == "R1" ? "10K 1%" : "");
    {
        const JPCplBomImport::Report r = imp.check();
        assert(r.conflicts.size() == 1 && r.conflicts[0].designator == "R1" && r.conflicts[0].field == I::Value);
    }

    // The board: one library part (by its MPN), the rest not chosen yet, the library untouched.
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "jplacer-test-cpl-bom";
    std::filesystem::create_directories(dir);
    JPConfiguration config(dir.string());
    auto lm358 = std::make_shared<JPPart>();
    lm358->id = "LM358DR";
    config.addPart(lm358);
    JPBoard board;
    JPCplBomImport::Report report;
    std::string error;
    assert(imp.build(config, "2026-10-08T12:00:00", board, report, error));
    assert(report.placements == 7 && board.placements.size() == 7 && report.doNotPlace == 1);
    // Grouped by MPN: C1 and C2 one part; R1 and R2 one (R1's value differs in the CPL: the BOM's wins).
    const JPPlacement* c1 = board.find("C1");
    const JPPlacement* c2 = board.find("C2");
    assert(c1 && c2 && c1->boardPart == c2->boardPart && board.find("R1")->boardPart == board.find("R2")->boardPart);
    const JPBoardPart* cap = board.part(c1->boardPart);
    assert(cap->field("mpn") == "CL10B104KB8NNNC" && cap->field("manufacturer") == "Samsung" && cap->field("value") == "100n");
    assert(cap->field("supplierPn") == "C14663" && cap->field("supplier") == "LCSC" && cap->field("extra:Voltage") == "50V");
    assert(cap->state == JPBoardPart::State::Unmatched && cap->partId() == "CL10B104KB8NNNC");
    assert(board.part(board.find("R1")->boardPart)->field("value") == "10k");
    const JPBoardPart* op = board.part(board.find("U1")->boardPart);
    assert(op->state == JPBoardPart::State::Matched && op->libraryPartId == "LM358DR" && board.find("U1")->partId == "LM358DR");
    assert(config.parts().size() == 1 && report.matched == 1);
    // Placements: positions, sides, a fiducial, the do-not-place one not enabled.
    assert(near(c2->location.x(), 12) && near(c2->location.rotation(), 90) && board.find("U1")->side == JPSide::Bottom);
    assert(board.find("FID1")->type == JPPlacement::Type::Fiducial && !board.find("J1")->enabled && c1->enabled);
    // The CPL chosen to win for values: the part R1 and R2 share (one MPN) takes R1's "10K 1%".
    {
        JPCplBomImport cplWins = imp;
        cplWins.winners[I::Value] = 0;
        JPBoard b2;
        JPCplBomImport::Report r2;
        assert(cplWins.build(config, "t", b2, r2, error));
        assert(b2.find("R1")->boardPart == b2.find("R2")->boardPart && b2.part(b2.find("R1")->boardPart)->field("value") == "10K 1%");
    }
    // Create Missing Parts: the board's own, its height from the file.
    {
        JPCplBomImport make = imp;
        make.createMissing = true;
        JPBoard b3;
        JPCplBomImport::Report r3;
        assert(make.build(config, "t", b3, r3, error));
        const JPBoardPart* own = b3.part(b3.find("C1")->boardPart);
        assert(own->state == JPBoardPart::State::Local && own->localPart->id == "CL10B104KB8NNNC" && config.parts().size() == 1);
    }
    // The files kept with the board: each one's mapping by header and every row, through its JSON.
    assert(board.provenance.size() == 2);
    const JPBoard back = JPBoard::fromJson(JJson::parse(board.toJson().dump()));
    assert(back.provenance.size() == 2 && back.provenance[1]["role"].str() == "bom");
    assert(back.provenance[1]["rows"].size() == 5 && back.provenance[1]["columns"][5]["field"].str() == "manufacturer");
    // No designator column: the CPL cannot make a board.
    {
        JPCplBomImport bad;
        bad.sources.push_back(source("Foo,Mid X,Mid Y\n1,2,3\n", JPImportSource::Role::Cpl, "bad.csv"));
        JPBoard b4;
        JPCplBomImport::Report r4;
        assert(!bad.build(config, "t", b4, r4, error) && error.find("Designator") != std::string::npos);
    }
    // Do not place, said either way.
    assert(JPCplBomImport::doNotPlace("DNP", "x") && !JPCplBomImport::doNotPlace("DNP", "") && !JPCplBomImport::doNotPlace("DNP", "No"));
    assert(JPCplBomImport::doNotPlace("Populate", "No") && !JPCplBomImport::doNotPlace("Fitted", "Yes"));

    // Profiles: a confirmed mapping kept, found for a file with its headers, applied.
    {
        JPMappingProfiles profiles;
        profiles.put(JPMappingProfiles::from(files[1], "My BOM"));
        const std::string path = (dir / JPMappingProfiles::kFile).string();
        assert(profiles.save(path, error));
        JPMappingProfiles read;
        assert(read.load(path, error) && read.profiles.size() == 1);
        JPImportSource next = source("Comment,Designator,Footprint,LCSC Part #,MPN,Provider,Voltage,DNP,Extra Col\n",
                                     JPImportSource::Role::Bom, "next.csv");
        assert(next.mapping[5] == I::Extra);
        const JPMappingProfiles::Profile* p = read.best(next.table.header, JPImportSource::Role::Bom);
        assert(p && p->name == "My BOM");
        JPMappingProfiles::apply(*p, next);
        assert(next.mapping[5] == I::Manufacturer && next.profile == "My BOM" && next.mapping[8] == I::Extra);
        assert(!read.best({ "Comment", "Designator" }, JPImportSource::Role::Bom));
    }
    std::filesystem::remove_all(dir);
    return 0;
}
