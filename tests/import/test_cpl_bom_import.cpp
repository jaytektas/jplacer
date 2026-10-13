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

#include <algorithm>
#include <filesystem>

using namespace jf;
using I = JPImportField::Id;

namespace {

// jplacer's own fields, as shipped: what the columns are guessed against.
const JPFields& shipped() {
    static const JPFields f;
    return f;
}

JPImportSource source(const std::string& text, JPImportSource::Role role, const std::string& name,
                      const JPFields& fields = shipped()) {
    JPImportSource s;
    s.role = role;
    std::string error;
    assert(JPTableFile::parse(text, s.table, error));
    s.table.path = "/cad/" + name;
    s.guess(fields);
    return s;
}

// The field of jplacer's own a header is guessed to be (Ignore: none).
I guessed(const std::string& header) { return JPImportField::idOf(JPImportField::guess(shipped(), header, {})); }

bool near(double a, double b) { return std::abs(a - b) < 1e-6; }

} // namespace

int main() {
    // Headers as tools write them.
    assert(guessed("Ref-X(mm)") == I::X && guessed("Mid Y") == I::Y);
    assert(guessed("PosX") == I::X && guessed("LCSC Part #") == I::SupplierPn);
    assert(guessed("Mfr. Part #") == I::Mpn && guessed("Manufacturer") == I::Manufacturer);
    assert(guessed("Comment") == I::Value && JPImportField::guess(shipped(), "Voltage", {}).empty());
    assert(guessed("Layer") == I::Side && guessed("Do Not Place") == I::DoNotPlace);

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
        assert(k.units == JPLengthUnit::Millimeters && k.table.comments.size() == 3);
        // In inches, as its comment says (KiCad 9): the lengths read as inches.
        const JPImportSource i = source("### Footprint positions ###\n## Unit = inches, Angle = deg.\n## Side : All\n"
                                        "# Ref  Val   Package             PosX     PosY     Rot  Side\n"
                                        "C1     10pF  C_0603_1608Metric   1.7291   2.5463  -90.0  top\n",
                                        JPImportSource::Role::Cpl, "i.pos");
        assert(i.units == JPLengthUnit::Inches);
        double mm = 0;
        assert(JPImportSource::length(i.table.rows[0][3], i.units, mm) && near(mm, 1.7291 * 25.4));
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
    files.push_back(source("Comment,Designator,Footprint,LCSC Part #,MPN,Provider,Voltage,DNP,Description\n"
                           "100n,\"C1,C2\",C0603,C14663,CL10B104KB8NNNC,Samsung,50V,,100nF 50V X7R\n"
                           "10k,R1-R2,R0603,C25804,0603WAF1002T5E,UniOhm,,,\n"
                           "LM358,U1,SOIC-8,C7950,LM358DR,TI,,,Dual op-amp\n"
                           "22u,C9,C0805,C45783,CL21A226MAQNNNE,Samsung,,,\n"
                           "NC,J1,Conn,,,,,DNP,\n",
                           JPImportSource::Role::Bom, "bom.csv"));
    assert(files[1].column(I::SupplierPn) == 3 && files[1].column(I::Mpn) == 4 && files[1].mapping[5].empty()
           && files[1].column(I::Description) == 8);
    files[1].mapping[5] = JPImportField::target(I::Manufacturer);   // the user's choice: "Provider" is the manufacturer

    JPCplBomImport imp;
    imp.sources = files;
    // The join: C9 in the BOM only; FID1 (a fiducial) not counted as missing from it.
    {
        const JPCplBomImport::Report r = imp.check();
        assert((r.otherOnly == std::vector<std::string>{ "C9" }) && r.cplOnly.empty() && r.placements == 7);
    }
    // A field the files disagree on: the BOM's value wins by default; the CPL's when chosen.
    imp.sources[0].table.header.push_back("Val");
    imp.sources[0].mapping.push_back(JPImportField::target(I::Value));
    for (auto& row : imp.sources[0].table.rows) row.push_back(row[0] == "R1" ? "10K 1%" : "");
    {
        const JPCplBomImport::Report r = imp.check();
        assert(r.conflicts.size() == 1 && r.conflicts[0].designator == "R1" && r.conflicts[0].field == JPImportField::target(I::Value));
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
    assert(cap->field("supplierPn") == "C14663" && cap->field("supplier") == "LCSC");
    // Named (no Name column) by its MPN, footprint and value.
    assert(cap->state == JPBoardPart::State::Unmatched && cap->partId() == "CL10B104KB8NNNC-C0603-100n");
    assert(board.part(board.find("R1")->boardPart)->field("value") == "10k");
    const JPBoardPart* op = board.part(board.find("U1")->boardPart);
    assert(op->state == JPBoardPart::State::Matched && op->libraryPartId == "LM358DR" && board.find("U1")->partId == "LM358DR");
    assert(config.parts().size() == 1 && report.matched == 1);
    // The CAD's rotation kept with each.
    assert(c2->cadRotation && *c2->cadRotation == 90);
    // Placements: positions, sides, a fiducial, the do-not-place one not enabled.
    assert(near(c2->location.x(), 12) && near(c2->location.rotation(), 90) && board.find("U1")->side == JPSide::Bottom);
    assert(board.find("FID1")->type == JPPlacement::Type::Fiducial && !board.find("J1")->enabled && c1->enabled);
    // The CPL chosen to win for values: the part R1 and R2 share (one MPN) takes R1's "10K 1%".
    {
        JPCplBomImport cplWins = imp;
        cplWins.winners[JPImportField::target(I::Value).text()] = 0;
        JPBoard b2;
        JPCplBomImport::Report r2;
        assert(cplWins.build(config, "t", b2, r2, error));
        assert(b2.find("R1")->boardPart == b2.find("R2")->boardPart && b2.part(b2.find("R1")->boardPart)->field("value") == "10K 1%");
    }
    // A footprint whose zero rotation is a quarter: the one placed with turned by it, the CAD's kept.
    {
        auto pkg = std::make_shared<JPPackage>();
        pkg->id = "SOIC-8";
        config.addPackage(pkg);
        config.part("LM358DR")->packageId = "SOIC-8";
        auto f = std::make_shared<JPLibraryFootprint>();
        f->name = "SOIC-8";
        f->packageId = "SOIC-8";
        f->zeroRotationDeg = -90;
        config.addFootprint(f);
        JPBoard turned;
        JPCplBomImport::Report rt;
        assert(imp.build(config, "t", turned, rt, error));
        const JPPlacement* u1 = turned.find("U1");
        assert(u1->cadRotation && *u1->cadRotation == 180 && std::abs(u1->location.rotation() - 90) < 1e-9);
    }
    // The sides imported: the top alone leaves U1 (the bottom's) out, counted; the bottom alone, U1 only.
    {
        JPCplBomImport top = imp;
        top.sides = JPCplBomImport::Sides::Top;
        JPBoard bt;
        JPCplBomImport::Report rt;
        assert(top.build(config, "t", bt, rt, error));
        assert(rt.placements == 6 && rt.otherSide == 1 && !bt.find("U1") && bt.find("C1") && top.check().otherSide == 1);
        JPCplBomImport under = imp;
        under.sides = JPCplBomImport::Sides::Bottom;
        JPBoard bb;
        JPCplBomImport::Report rb;
        assert(under.build(config, "t", bb, rb, error));
        assert(rb.placements == 1 && rb.otherSide == 6 && bb.find("U1") && bb.placements.size() == 1);
    }
    // The library's part it is matched to, as the import is taken: what it has none of filled (its description,
    // MPN, supplier's part number), once; what it has kept, the files' differing said.
    {
        JPPart* op = config.libraryPart("LM358DR");
        assert(!op->name && op->identifiers.empty());
        int packages = 0;
        config.takeMade(board, packages);
        assert(op->name == std::optional<std::string>("Dual op-amp") && op->identifiers.size() == 2);
        assert(op->identifiers[0].kind == "mpn" && op->identifiers[0].org == "TI" && op->identifiers[1].code == "C7950");
        op->name = "My op-amp";
        JPBoard again;
        JPCplBomImport::Report ra;
        assert(imp.build(config, "t", again, ra, error));
        assert(std::any_of(ra.fieldNotes.begin(), ra.fieldNotes.end(),
                           [](const std::string& n) { return n.find("the library has \"My op-amp\"") != std::string::npos; }));
        config.takeMade(again, packages);
        assert(op->name == std::optional<std::string>("My op-amp") && op->identifiers.size() == 2);
    }
    // Create Missing Parts: a library part made for it (not in the library until the import is taken), the
    // board part matched to it; put in the library, it is the library's.
    {
        JPCplBomImport make = imp;
        make.createMissing = true;
        JPBoard b3;
        JPCplBomImport::Report r3;
        assert(make.build(config, "t", b3, r3, error));
        const JPBoardPart* made = b3.part(b3.find("C1")->boardPart);
        // Named as the files name it, known by a GUID, the board part linked by it, the name its AKA.
        const auto part = std::find_if(b3.madeParts.begin(), b3.madeParts.end(),
                                       [](const auto& p) { return p->label == "CL10B104KB8NNNC-C0603-100n"; });
        assert(part != b3.madeParts.end() && (*part)->id.size() == 36 && !(*part)->akas.empty());
        // What the files say of it: its description (shown as the Parts table's Description), its MPN and LCSC number.
        assert((*part)->name == std::optional<std::string>("100nF 50V X7R"));
        assert(std::any_of((*part)->identifiers.begin(), (*part)->identifiers.end(),
                           [](const auto& i) { return i.kind == "mpn" && i.org == "Samsung" && i.code == "CL10B104KB8NNNC"; }));
        assert(std::any_of((*part)->identifiers.begin(), (*part)->identifiers.end(),
                           [](const auto& i) { return i.kind == "supplierPn" && i.code == "C14663"; }));
        assert(made->state == JPBoardPart::State::Matched && made->libraryPartId == (*part)->id && r3.made > 0);
        assert(config.parts().size() == 1 && !b3.madeParts.empty());   // the library untouched by building it
        int packages = 0;
        assert(config.takeMade(b3, packages) == int(b3.madeParts.size()) && config.partNamed("CL10B104KB8NNNC-C0603-100n"));
        assert(config.takeMade(b3, packages) == 0);   // taken once
        for (const auto& p : b3.madeParts) config.removePart(p->id);
        for (const auto& k : b3.madePackages) config.removePackage(k->id);
    }
    // The files kept with the board: each one's mapping by header and every row, through its JSON.
    assert(board.provenance.size() == 2);
    const JPBoard back = JPBoard::fromJson(JJson::parse(board.toJson().dump()));
    assert(back.provenance.size() == 2 && back.provenance[1]["role"].str() == "bom");
    assert(back.provenance[1]["rows"].size() == 5 && back.provenance[1]["columns"][5]["field"].str() == "parts:manufacturer");
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
        JPImportSource next = source("Comment,Designator,Footprint,LCSC Part #,MPN,Provider,Voltage,DNP,Description,Extra Col\n",
                                     JPImportSource::Role::Bom, "next.csv");
        assert(next.mapping[5].empty());
        const JPMappingProfiles::Profile* p = read.best(next.table.header, JPImportSource::Role::Bom);
        assert(p && p->name == "My BOM");
        JPMappingProfiles::apply(*p, next, shipped());
        assert(next.mapping[5] == JPImportField::target(I::Manufacturer) && next.profile == "My BOM" && next.mapping[9].empty());
        assert(!read.best({ "Comment", "Designator" }, JPImportSource::Role::Bom));
    }
    // The columns as last imported: kept without a name, found again only for exactly the same columns (a column
    // left out kept as left out), and taken over by the next import of them.
    {
        JPMappingProfiles profiles;
        JPImportSource taken = source("Comment,Designator,Footprint\n10k,R1,0603\n", JPImportSource::Role::Bom, "a.csv");
        taken.mapping[0] = JPImportField::target(I::Name);
        taken.mapping[2] = {};
        profiles.remember(taken);
        const std::string path = (dir / JPMappingProfiles::kFile).string();
        assert(profiles.save(path, error));
        JPMappingProfiles read;
        assert(read.load(path, error) && read.profiles.empty() && read.last.size() == 1);
        JPImportSource next = source("Comment,Designator,Footprint\n1u,C1,0805\n", JPImportSource::Role::Bom, "b.csv");
        const JPMappingProfiles::Profile* p = read.lastFor(next.table.header, JPImportSource::Role::Bom);
        assert(p && !read.lastFor(next.table.header, JPImportSource::Role::Cpl));
        assert(!read.lastFor({ "Comment", "Designator" }, JPImportSource::Role::Bom));
        assert(!read.lastFor({ "Comment", "Designator", "Footprint", "Extra" }, JPImportSource::Role::Bom));
        JPMappingProfiles::apply(*p, next, shipped());
        assert(next.mapping[0] == JPImportField::target(I::Name) && next.mapping[2].empty());
        assert(next.profile == JPMappingProfiles::kLast);
        next.mapping[0] = JPImportField::target(I::Value);
        read.remember(next);
        assert(read.last.size() == 1 && read.last[0].columns.at("Comment") == JPImportField::target(I::Value).text());
    }
    // The fields added: a placement's, a part's and its package's from their rows; the board's and the job's one
    // value for the whole import (a column's, else what a file says of itself, else as typed, over either).
    {
        JPFields& f = config.fields();
        auto add = [&f](JPFieldSet::Of of, const std::string& name, JPField::Type type, std::vector<std::string> matches = {}) {
            JPField x;
            x.name = name;
            x.type = type;
            x.matches = std::move(matches);
            return f.of(of).add(x);
        };
        const std::string lane = add(JPFieldSet::Of::Placements, "Feed Lane", JPField::Type::Number);
        const std::string volts = add(JPFieldSet::Of::Parts, "Voltage", JPField::Type::Text);
        const std::string pitch = add(JPFieldSet::Of::Packages, "Pin Pitch", JPField::Type::Length);
        const std::string rev = add(JPFieldSet::Of::Boards, "Revision", JPField::Type::Text, { "Rev" });
        const std::string order = add(JPFieldSet::Of::Job, "Order", JPField::Type::Text);
        f.changed();
        // A library part (its MPN) with a voltage of its own, and its package with no pitch yet.
        auto known = std::make_shared<JPPart>();
        known->id = "MPN9";
        known->packageId = "R0805";
        known->custom.set(volts, JJson("100V"));
        config.addPart(known);
        auto r0805 = std::make_shared<JPPackage>();
        r0805->id = "R0805";
        config.addPackage(r0805);

        JPCplBomImport im;
        im.sources.push_back(source("Designator,Mid X,Mid Y,Layer,Rotation,Feed Lane,Rev\n"
                                    "R1,1,1,Top,0,3,B\nR2,2,1,Top,0,x,C\nR3,3,1,Top,0,,\n",
                                    JPImportSource::Role::Cpl, "fields.csv", f));
        im.sources.push_back(source("Designator,Comment,Footprint,MPN,Voltage,Pin Pitch\n"
                                    "R1,10k,R0603X,MPN1,50V,0.5mm\nR2,10k,R0603X,MPN1,25V,0.5mm\nR3,1k,R0805,MPN9,50V,1.27mm\n",
                                    JPImportSource::Role::Bom, "fields-bom.csv", f));
        assert(im.sources[0].mapping[5] == (JPImportField::Target { JPFieldSet::Of::Placements, lane }));
        assert(im.sources[0].mapping[6] == (JPImportField::Target { JPFieldSet::Of::Boards, rev }));
        assert(im.sources[1].mapping[4] == (JPImportField::Target { JPFieldSet::Of::Parts, volts }));
        assert(im.sources[1].mapping[5] == (JPImportField::Target { JPFieldSet::Of::Packages, pitch }));
        // A field of your own made for a heading takes it over jplacer's that matches it too.
        {
            JPFields own;
            JPField lcsc;
            lcsc.name = "LCSC";
            const std::string key = own.of(JPFieldSet::Of::Parts).add(lcsc);
            assert(JPImportField::guess(own, "LCSC", {}) == (JPImportField::Target { JPFieldSet::Of::Parts, key }));
            assert(JPImportField::guess(shipped(), "LCSC", {}) == JPImportField::target(I::SupplierPn));
        }
        // What the placement file says of itself names the job's order.
        im.sources[0].about.push_back({ "Order", "PO-7" });
        im.createMissing = true;
        JPBoard b;
        JPCplBomImport::Report r;
        assert(im.build(config, "t", b, r, error));
        auto noted = [&r](const std::string& part) {
            return std::any_of(r.fieldNotes.begin(), r.fieldNotes.end(), [&part](const std::string& n) { return n.find(part) != std::string::npos; });
        };
        // The placement's own; one its type cannot read noted, not taken.
        assert(b.find("R1")->custom.get(lane).number() == 3 && !b.find("R2")->custom.has(lane) && noted("not a number"));
        // The part made: the first placement's value, the other's noted; its package made with its pitch.
        const auto made = std::find_if(b.madeParts.begin(), b.madeParts.end(), [](const auto& p) { return p->label == "MPN1-R0603X-10k"; });
        assert(made != b.madeParts.end() && (*made)->custom.get(volts).str() == "50V" && noted("25V"));
        const auto pkg = std::find_if(b.madePackages.begin(), b.madePackages.end(), [](const auto& k) { return k->label == "R0603X"; });
        assert(pkg != b.madePackages.end() && (*pkg)->custom.get(pitch).number() == 0.5);
        // The library's part keeps its own (the files' differing said); its package's pitch is filled as it is taken.
        assert(noted("the library has \"100V\""));
        // The board: the column's first row, the other said; the job's from what the file says of itself.
        assert(b.custom.get(rev).str() == "B" && noted("2 different things") && r.job.get(order).str() == "PO-7");
        int packages = 0;
        config.takeMade(b, packages);
        assert(config.part("MPN9")->custom.get(volts).str() == "100V" && config.package("R0805")->custom.get(pitch).number() == 1.27);
        // Typed over the files; emptied, none.
        im.typedBoard[rev] = "D";
        im.typedJob[order] = "";
        JPBoard typed;
        JPCplBomImport::Report rt;
        assert(im.build(config, "t", typed, rt, error) && typed.custom.get(rev).str() == "D" && !rt.job.has(order));
        // The files kept with the board say which field each column filled.
        assert(typed.provenance[0]["columns"][5]["field"].str() == "placements:" + lane);
    }
    // A Name column (guessed by its heading): each part named so, rows of one name one part, a library part of that
    // name taken unasked.
    {
        auto mine = std::make_shared<JPPart>();
        mine->id = "11111111-2222-3333-4444-555555555555";
        mine->label = "Bulk Cap 10u";
        config.addPart(mine);
        JPCplBomImport im;
        im.sources.push_back(source("Designator,Mid X,Mid Y,Layer,Rotation\nC1,1,1,Top,0\nC2,2,1,Top,0\nC3,3,1,Top,0\n",
                                    JPImportSource::Role::Cpl, "named.csv"));
        im.sources.push_back(source("Designator,Comment,Footprint,Part Name\nC1,10u,C0805,Bulk Cap 10u\nC2,10u,C0805,Bulk Cap 10u\n"
                                    "C3,10u,C0603,Small Cap\n",
                                    JPImportSource::Role::Bom, "named-bom.csv"));
        assert(im.sources[1].mapping[3] == JPImportField::target(I::Name));
        im.createMissing = true;
        JPBoard b;
        JPCplBomImport::Report r;
        assert(im.build(config, "t", b, r, error));
        const JPBoardPart* bulk = b.part(b.find("C1")->boardPart);
        assert(b.find("C2")->boardPart == b.find("C1")->boardPart && bulk->libraryPartId == mine->id);
        assert(b.part(b.find("C3")->boardPart)->field("part") == "Small Cap" && r.made == 1);
        assert(std::any_of(b.madeParts.begin(), b.madeParts.end(), [](const auto& p) { return p->label == "Small Cap"; }));
    }
    // Two parts the files name alike (EasyEDA's CPL names a part by its value): an 0402 and an 0603 100nF, each its
    // own LCSC number. Two board parts, not one; the 0603 the library's (by its LCSC number), the 0402 not taken
    // by the name the library's 0603 has, but made, told apart by its footprint.
    {
        auto pkg = std::make_shared<JPPackage>();
        pkg->id = "pkg-c0603";
        pkg->label = "C0603";
        config.addPackage(pkg);
        auto c0603 = std::make_shared<JPPart>();
        c0603->id = "aaaaaaaa-2222-3333-4444-555555555555";
        c0603->label = "100nF";
        c0603->packageId = pkg->id;
        c0603->identifiers.push_back({ "supplierPn", "LCSC", "C14663" });
        config.addPart(c0603);
        JPCplBomImport im;
        im.sources.push_back(source("Designator,Mid X,Mid Y,Layer,Rotation\nC4,1,1,Top,0\nC5,2,1,Top,0\nC1,3,1,Top,0\n",
                                    JPImportSource::Role::Cpl, "alike.csv"));
        im.sources.push_back(source("Designator,Footprint,Part Name,Supplier Part\nC4,C0402,100nF,C131394\nC5,C0402,100nF,C131394\n"
                                    "C1,C0603,100nF,C14663\n",
                                    JPImportSource::Role::Bom, "alike-bom.csv"));
        im.createMissing = true;
        JPBoard b;
        JPCplBomImport::Report r;
        assert(im.build(config, "t", b, r, error));
        const JPBoardPart* small = b.part(b.find("C4")->boardPart);
        const JPBoardPart* big = b.part(b.find("C1")->boardPart);
        assert(small && big && small != big && b.find("C5")->boardPart == b.find("C4")->boardPart);
        assert(big->libraryPartId == c0603->id);
        assert(small->state == JPBoardPart::State::Unmatched || small->libraryPartId != c0603->id);
        assert(small->field("part") == "100nF-C0402");
        assert(std::any_of(b.madeParts.begin(), b.madeParts.end(), [](const auto& p) { return p->label == "100nF-C0402"; }));
    }
    std::filesystem::remove_all(dir);
    return 0;
}
