// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// A board saved before the library was the only place of parts, opened: its own part and package put in the
// library under their names without the board's, a copy of a part this library lacks put in it, a copy of one
// it has let go; each board part then matched to the library's, its placements placed with it; the board
// marked changed and, saved, keeping no part or package of its own.
// Tests check with assert(); a Release build must not compile it away.
#undef NDEBUG
#include <cassert>

#include "model/JPConfiguration.h"
#include "model/JPLibraryJson.h"

#include <filesystem>

using namespace jf;
namespace fs = std::filesystem;

int main() {
    const fs::path dir = fs::temp_directory_path() / "jplacer-test-board-former";
    fs::remove_all(dir);
    fs::create_directories(dir);
    JPConfiguration config(dir.string());
    auto r0603 = std::make_shared<JPPart>();
    r0603->id = "R0603-1K";
    r0603->packageId = "R0603";
    config.addPart(r0603);
    auto k0603 = std::make_shared<JPPackage>();
    k0603->id = "R0603";
    config.addPackage(k0603);

    // As a board was saved: its own 100 nF cap (scoped ids, its own package), a copy of R0603-1K (the library
    // has it), a copy of LED-RED (it has not).
    JPPart own;
    own.id = "old/C_0603-100n";
    own.value = "100n";
    own.packageId = "old/C_0603";
    JPPackage ownK;
    ownK.id = "old/C_0603";
    ownK.footprint.pads.push_back({ "1", -0.8, 0, 0.9, 0.95, 0, false, 25 });
    JPPart led;
    led.id = "LED-RED";
    led.packageId = "LED_0805";
    JPPackage ledK;
    ledK.id = "LED_0805";
    JJson parts = JJson::array();
    auto part = [&parts](const std::string& key, const std::string& name, JJson resolution) {
        JJson p = JJson::object();
        p["key"] = key;
        JJson f = JJson::object();
        f["part"] = name;
        f["value"] = "100n";
        p["fields"] = f;
        p["resolution"] = resolution;
        parts.push(p);
    };
    JJson local = JJson::object();
    local["state"] = "local";
    local["part"] = JPLibraryJson::part(own);
    local["package"] = JPLibraryJson::package(ownK);
    part("bp-1", "C_0603-100n", local);
    JJson have = JJson::object();
    have["state"] = "matched";
    have["libraryId"] = "R0603-1K";
    JJson haveCopy = JJson::object();
    haveCopy["part"] = JPLibraryJson::part(*r0603);
    have["copy"] = haveCopy;
    part("bp-2", "R0603-1K", have);
    JJson lacks = JJson::object();
    lacks["state"] = "matched";
    lacks["libraryId"] = "LED-RED";
    JJson lacksCopy = JJson::object();
    lacksCopy["part"] = JPLibraryJson::part(led);
    lacksCopy["package"] = JPLibraryJson::package(ledK);
    lacks["copy"] = lacksCopy;
    part("bp-3", "LED-RED", lacks);
    JPBoard b;
    b.name = "old";
    for (const auto& [id, key] : { std::pair { "C1", "bp-1" }, std::pair { "R1", "bp-2" }, std::pair { "D1", "bp-3" } }) {
        JPPlacement p;
        p.id = id;
        p.boardPart = key;
        b.placements.push_back(p);
    }
    JJson j = b.toJson();
    j["parts"] = parts;
    const std::string file = (dir / "old.jpboard").string();
    assert(j.dumpToFile(file));

    std::string error;
    const auto opened = config.board(file, error);
    assert(opened && opened->dirty);
    // Its own part and package the library's, by their names without the board's.
    const JPPart* cap = config.libraryPart("C_0603-100n");
    assert(cap && cap->packageId == "C_0603" && cap->value == "100n" && !cap->uuid.empty());
    assert(config.libraryPackage("C_0603") && config.libraryPackage("C_0603")->footprint.pads.size() == 1);
    // The copy of the part the library lacks, the library's; the one it has, let go.
    assert(config.libraryPart("LED-RED") && config.libraryPackage("LED_0805") && config.parts().size() == 3);
    for (const JPBoardPart& bp : opened->parts()) {
        assert(bp.state == JPBoardPart::State::Matched && !bp.former && config.libraryPartFor(bp));
        assert(bp.libraryPartId == "C_0603-100n" || bp.libraryPartId == "R0603-1K" || bp.libraryPartId == "LED-RED");
    }
    assert(opened->find("C1")->partId == "C_0603-100n" && opened->find("D1")->partId == "LED-RED");
    // Saved, it keeps nothing of its own.
    const std::string saved = opened->toJson().dump();
    assert(saved.find("\"copy\"") == std::string::npos && saved.find("\"local\"") == std::string::npos);

    fs::remove_all(dir);
    return 0;
}
