// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// Fields (DESIGN.md, Fields): each kind's built-ins, those added with a key of their own never given again, a
// required one never removed, a built-in removed and restored, renamed and given matches; fields.json keeps it
// all. Each type's values read and shown; an import heading matched. And a thing's values of the fields added
// kept with it in its file: a part's, a package's and its pads' in the library (a library of schema 5 brought
// up to 6, not refused), a placement's and a board's in the board file, a board location's and the job's in
// the job, a feeder's in feeders.json, vision settings' in vision-settings.json.
// Tests check with assert(); a Release build must not compile it away.
#undef NDEBUG
#include <cassert>
#include <cmath>
#include <cstdio>

#include "model/JPConfiguration.h"

#include <j/db/Database.h>

#include <filesystem>

using namespace jf;
namespace fs = std::filesystem;
using O = JPFieldSet::Of;

namespace {

JJson parsed(const JPField& f, const std::string& text) {
    JJson v;
    std::string error;
    assert(f.parse(text, v, error) || (std::fprintf(stderr, "%s: %s\n", text.c_str(), error.c_str()), false));
    return v;
}

bool refused(const JPField& f, const std::string& text) {
    JJson v;
    std::string error;
    return !f.parse(text, v, error) && !error.empty();
}

JPConfiguration* loaded(JPConfiguration& c) {
    std::vector<std::string> problems;
    std::string error;
    assert(c.load(problems, error) || (std::fprintf(stderr, "%s\n", error.c_str()), false));
    return &c;
}

} // namespace

int main() {
    // Each type's values.
    {
        JPField f;
        f.name = "Qty";
        f.type = JPField::Type::Number;
        assert(parsed(f, "12").number() == 12 && parsed(f, "12,5").number() == 12.5 && refused(f, "twelve"));
        assert(parsed(f, "  ").isNull() && f.text(JJson(0.5)) == "0.5" && f.text(JJson()) == "");
        f.type = JPField::Type::Length;
        assert(std::fabs(parsed(f, "40mil").number() - 1.016) < 1e-9 && parsed(f, "1.2").number() == 1.2 && refused(f, "far"));
        f.type = JPField::Type::YesNo;
        assert(parsed(f, "yes").boolean() && !parsed(f, "No").boolean() && parsed(f, "x").boolean() && refused(f, "maybe"));
        assert(f.text(JJson(true)) == "Yes" && f.text(JJson(false)) == "");
        f.type = JPField::Type::Choice;
        f.choices = { "Reel", "Cut tape" };
        assert(parsed(f, "cut TAPE").str() == "Cut tape" && refused(f, "Tray"));
        f.type = JPField::Type::Text;
        assert(parsed(f, " C1525 ").str() == "C1525");
        // Matched by its name, or a heading it was given (units, case and punctuation aside).
        f.name = "LCSC";
        f.matches = { "JLC Part #" };
        assert(f.matchesHeading("lcsc") && f.matchesHeading("JLC part#") && !f.matchesHeading("Value"));
        JPField h;
        h.name = "Clearance";
        h.matches = { "Clearance" };
        assert(h.matchesHeading("Clearance (mm)"));
    }

    // A kind's fields: built-ins, those added, required ones kept, removed ones restored, names unique.
    {
        JPFieldSet parts(O::Parts);
        assert(parts.has("height") && parts.field("height")->required && parts.field("height")->builtIn);
        assert(parts.field("footprint") && parts.field("footprint")->imported() && !parts.field("speed")->imported());
        assert(!parts.remove("height") && !parts.remove("package") && !parts.remove("id"));
        assert(parts.remove("name") && !parts.has("name") && parts.field("name")->removed && !parts.named("Name"));
        parts.restore("name");
        assert(parts.has("name"));
        JPField lcsc;
        lcsc.name = "LCSC";
        const std::string k1 = parts.add(lcsc);
        std::string error;
        assert(k1 == "u1" && parts.named("lcsc") && parts.added().size() == 1);
        assert(!parts.nameFree("height", "", error) && !parts.nameFree("lcsc", "", error) && parts.nameFree("lcsc", k1, error));
        assert(!parts.nameFree("a:b", "", error) && !parts.nameFree("  ", "", error));
        // Taken out and another added: a new key, never the old one again.
        assert(parts.remove(k1) && !parts.field(k1));
        JPField rev;
        rev.name = "Revision";
        const std::string k2 = parts.add(rev);
        assert(k2 == "u2");
        // A built-in renamed and given a heading; its type is its own.
        JPField height = *parts.field("height");
        height.name = "Part Height";
        height.matches.push_back("Z Height");
        height.type = JPField::Type::Text;
        assert(parts.update(height, error) && parts.named("Part Height") && parts.field("height")->matchesHeading("Z Height"));
        assert(!parts.named("Height"));
        // Kept and read back, over the built-ins.
        assert(parts.remove("value"));
        JPFieldSet back(O::Parts);
        back.fromJson(JJson::parse(parts.toJson().dump()));
        assert(back.named("Part Height") && !back.has("value") && back.has("name") && back.named("Revision")->key == "u2");
        JPField third;
        third.name = "Third";
        assert(back.add(third) == "u3");
        // A built-in as shipped is not kept.
        assert(JPFieldSet(O::Feeders).toJson()["builtIn"].arr().empty());
    }

    const fs::path dir = fs::temp_directory_path() / "jplacer-test-fields";
    fs::remove_all(dir);
    fs::create_directories(dir);

    // fields.json, and every thing's values kept with it.
    std::string partId, boardFile = (dir / "b.jpboard").string(), jobFile = (dir / "j.jpjob").string();
    {
        JPConfiguration c(dir.string());
        loaded(c);
        JPField f;
        f.name = "LCSC";
        const std::string key = c.fields().of(O::Parts).add(f);
        assert(c.fields().of(O::Parts).remove("value"));
        auto part = std::make_shared<JPPart>();
        part->id = partId = "11111111-2222-3333-4444-555555555555";
        part->label = "R1k";
        part->custom.set(key, JJson("C1525"));
        c.addPart(part);
        auto k = std::make_shared<JPPackage>();
        k->id = "R0603";
        k->custom.set("u1", JJson(4.0));
        k->footprint.pads.push_back({ "1", 0, 0, 1, 1, 0, true, 0, {} });
        k->footprint.pads.push_back({ "2", 1, 0, 1, 1, 0, false, 0, {} });
        k->footprint.pads[1].custom.set("u1", JJson(true));
        c.addPackage(k);
        JPFeeder fd = JPFeeder::create("org.openpnp.machine.reference.feeder.ReferenceStripFeeder", partId);
        fd.custom.set("u1", JJson("Reel 7"));
        c.addFeeder(std::move(fd));
        JPVisionSettings v = JPVisionSettings::create(JPVisionSettings::Kind::Bottom, "BVS_Mine");
        v.custom.set("u1", JJson("tuned"));
        c.addVisionSettings(v);
        std::string error;
        assert(c.save(error));
        // A board's placement and the board; the job's board location and the job.
        auto board = c.board(boardFile, error);
        JPPlacement p;
        p.id = "R1";
        p.custom.set("u1", JJson(3.0));
        board->placements.push_back(p);
        board->custom.set("u1", JJson("Rev C"));
        assert(c.saveBoard(*board, error));
        JPJob job;
        auto l = std::make_unique<JPBoardLocation>();
        l->fileName = boardFile;
        l->custom.set("u1", JJson("Panel A"));
        job.root().addChild(std::move(l));
        job.custom.set("u1", JJson("Order 42"));
        assert(c.saveJob(job, jobFile, error));
    }
    {
        JPConfiguration c(dir.string());
        loaded(c);
        assert(c.fields().of(O::Parts).named("LCSC") && !c.fields().of(O::Parts).has("value"));
        assert(c.part(partId)->custom.get("u1").str() == "C1525");
        const JPPackage* k = c.package("R0603");
        assert(k->custom.get("u1").number() == 4 && k->footprint.pads.size() == 2 && k->footprint.pads[1].custom.get("u1").boolean()
               && k->footprint.pads[0].custom.empty());
        assert(c.feeders().back().custom.get("u1").str() == "Reel 7");
        assert(c.visionSettings("BVS_Mine")->custom.get("u1").str() == "tuned");
        std::string error;
        auto board = c.board(boardFile, error);
        assert(board && board->custom.get("u1").str() == "Rev C" && board->find("R1")->custom.get("u1").number() == 3);
        auto job = c.loadJob(jobFile, error);
        assert(job && job->custom.get("u1").str() == "Order 42");
        assert(job->root().children().front()->custom.get("u1").str() == "Panel A");
    }

    // A library of schema 5 (no columns for the fields added, packages known by name): brought up to 7, its parts
    // kept, naming their packages by GUID.
    {
        const fs::path old = dir / "old";
        fs::create_directories(old);
        {
            JDatabase db;
            assert(db.open((old / JPLibraryStore::kFile).string()));
            assert(db.exec("CREATE TABLE meta(key TEXT PRIMARY KEY, value TEXT);"
                           "CREATE TABLE packages(uuid TEXT PRIMARY KEY, id TEXT NOT NULL UNIQUE COLLATE NOCASE, data TEXT NOT NULL);"
                           "CREATE TABLE parts(id TEXT PRIMARY KEY, label TEXT NOT NULL UNIQUE COLLATE NOCASE, name TEXT, value TEXT, "
                           "datasheet TEXT, package_id TEXT, last_packaging INTEGER, data TEXT NOT NULL);"
                           "INSERT INTO meta VALUES('schema', '5');"
                           "INSERT INTO packages VALUES('9f1c2a3b-0000-4000-8000-000000000001', 'R0603', '{\"id\":\"R0603\"}');"
                           "INSERT INTO parts VALUES('p-1', 'R1k', '', '1k', '', 'R0603', -1, "
                           "'{\"id\":\"p-1\",\"package-id\":\"R0603\"}');"));
        }
        JPConfiguration c(old.string());
        loaded(c);
        assert(c.parts().size() == 1 && c.parts().front()->label == "R1k" && c.parts().front()->value == "1k");
        // Its packages known by GUIDs now (named as they were), its part naming its package so.
        const JPPackage* k = c.packageNamed("R0603");
        assert(k && k->id == "9f1c2a3b-0000-4000-8000-000000000001" && c.parts().front()->packageId == k->id);
        c.parts().front()->custom.set("u1", JJson("kept"));
        std::string error;
        assert(c.save(error));
        JPConfiguration again(old.string());
        loaded(again);
        assert(again.parts().front()->custom.get("u1").str() == "kept");
    }
    fs::remove_all(dir);
    return 0;
}
