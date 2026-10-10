// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// Every part and package open (JPCatalog): the library's, then an open board's own, its copy of a library
// part (Matched, then Library changed once the library's is edited), and one to be chosen; names shown without
// the board's; the search (a word anywhere, at a name's start ranked first, a value written another way, a
// kind's word); a board's own added to the library, the board keeping its own; a board's own package edited,
// every part of the board that shares it given the same; and a board's own part kept whole through its file.
// Tests check with assert(); a Release build must not compile it away.
#undef NDEBUG
#include <cassert>

#include "model/JPCatalog.h"
#include "openpnp/JPXmlJson.h"

#include <filesystem>

using namespace jf;
namespace fs = std::filesystem;

int main() {
    const fs::path dir = fs::temp_directory_path() / "jplacer-test-catalog";
    fs::remove_all(dir);
    fs::create_directories(dir);
    JPConfiguration config(dir.string());
    auto qfp = std::make_shared<JPPackage>();
    qfp->id = "LQFP-100_14x14mm_P0.5mm";
    config.addPackage(qfp);
    auto mcu = std::make_shared<JPPart>();
    mcu->id = "LQFP-100_14x14mm_P0.5mm-STM32F407VETx";
    mcu->packageId = qfp->id;
    config.addPart(mcu);

    std::string error;
    auto board = config.board((dir / "grblhal.jpboard").string(), error);
    assert(board);
    // Its own 100 nF cap in a package of its own (two parts sharing it), its copy of the MCU, and one not chosen.
    for (const char* value : { "100nF", "1uF" }) {
        JPBoardPart own;
        own.key = board->newPartKey();
        own.fields["part"] = std::string("C_0603_1608Metric-") + value;
        own.fields["value"] = value;
        own.state = JPBoardPart::State::Local;
        own.localPart = std::make_shared<JPPart>();
        own.localPart->id = "grblhal/C_0603_1608Metric-" + std::string(value);
        own.localPart->value = value;
        own.localPart->packageId = "grblhal/C_0603_1608Metric";
        own.localPackage = std::make_shared<JPPackage>();
        own.localPackage->id = "grblhal/C_0603_1608Metric";
        board->parts().push_back(own);
    }
    JPBoardPart matched;
    matched.key = board->newPartKey();
    matched.fields["part"] = mcu->id;
    matched.state = JPBoardPart::State::Matched;
    matched.libraryPartId = mcu->id;
    config.takeCopy(matched);
    board->parts().push_back(matched);
    JPBoardPart open;
    open.key = board->newPartKey();
    open.fields["part"] = "SW_Push-SW";
    open.fields["value"] = "SW";
    board->parts().push_back(open);

    auto parts = JPCatalog::parts(config);
    assert(parts.size() == 5);
    assert(parts[0].status == JPCatalog::Status::Library && !parts[0].board);
    assert(parts[1].status == JPCatalog::Status::Own && parts[1].name == "C_0603_1608Metric-100nF" && parts[1].board == board);
    assert(parts[3].status == JPCatalog::Status::Matched && parts[3].part() && parts[3].part() != mcu.get());
    assert(parts[4].status == JPCatalog::Status::ToBeChosen && !parts[4].part() && parts[4].name == "SW_Push-SW");
    assert(JPCatalog::source(parts[1].board) == "grblhal" && JPCatalog::source(parts[0].board) == "Library");
    assert(JPCatalog::editable(parts[1].status) && !JPCatalog::editable(parts[3].status));
    // The library's MCU changed: the board's copy says so.
    mcu->speed = 0.5;
    parts = JPCatalog::parts(config);
    assert(parts[3].status == JPCatalog::Status::LibraryChanged);
    JPCatalog::updateFromLibrary(config, parts[3]);
    assert(board->dirty && JPCatalog::parts(config)[3].status == JPCatalog::Status::Matched);

    // Packages: the library's, the board's own once (its two parts share it), its copy of the MCU's.
    auto packages = JPCatalog::packages(config);
    assert(packages.size() == 3 && packages[1].name == "C_0603_1608Metric" && packages[1].status == JPCatalog::Status::Own);
    assert(packages[2].status == JPCatalog::Status::Matched);

    // The search.
    parts = JPCatalog::parts(config);
    assert(JPCatalog::matches(config, parts[0], "lq") > 0 && JPCatalog::matches(config, parts[1], "lq") == 0);
    assert(JPCatalog::matches(config, parts[0], "lq") > JPCatalog::matches(config, parts[0], "ETx"));   // start, inside
    assert(JPCatalog::matches(config, parts[1], "0.1u") > 0 && JPCatalog::matches(config, parts[1], "0603 100n") > 0);
    assert(JPCatalog::matches(config, parts[2], "100n") == 0);   // the 1 µF
    assert(JPCatalog::matches(config, parts[1], "cap") > 0 && JPCatalog::matches(config, parts[0], "cap") == 0);
    assert(JPCatalog::matches(config, parts[4], "sw") > 0 && JPCatalog::matches(config, parts[1], "") > 0);
    assert(JPCatalog::matches(packages[0], "qfp") > 0 && JPCatalog::matches(packages[1], "qfp") == 0);

    // A board's own into the library: under its own name, its package too; the board keeps its own.
    assert(JPCatalog::addToLibrary(config, parts[1], error));
    assert(config.libraryPart("C_0603_1608Metric-100nF") && config.libraryPackage("C_0603_1608Metric"));
    assert(config.libraryPart("C_0603_1608Metric-100nF")->packageId == "C_0603_1608Metric");
    assert(board->parts()[0].state == JPBoardPart::State::Local);
    assert(!JPCatalog::addToLibrary(config, parts[1], error) && !error.empty());   // there already
    assert(!JPCatalog::addToLibrary(config, parts[3], error));                    // a copy, not its own

    // A board's own package edited: the other part sharing it given the same.
    board->parts()[0].localPackage->description = "edited";
    JPCatalog::shareEdit(*board, *board->parts()[0].localPackage);
    assert(board->parts()[1].localPackage->description.value_or("") == "edited");

    // A board's own part kept whole as the board is saved and read: its value and packaging too (the library's
    // form); one saved before (OpenPnP's part only) read, its value from what the files said.
    {
        JPBoardPart own = board->parts()[0];
        own.localPart->packagings.push_back({});
        own.localPart->packagings.back().tapeWidthMm = 12;
        const JPBoardPart back = JPBoardPart::fromJson(own.toJson());
        assert(back.localPart && back.localPart->value == "100nF" && back.localPart->packagings.size() == 1);
        assert(back.localPart->packagings[0].tapeWidthMm == 12 && back.localPackage && back.localPackage->description == "edited");
        JJson old = own.toJson();
        old["resolution"]["part"] = JPXmlJson::from(own.localPart->toXml());
        const JPBoardPart before = JPBoardPart::fromJson(old);
        assert(before.localPart && before.localPart->id == own.localPart->id && before.localPart->value == "100nF");
    }

    fs::remove_all(dir);
    return 0;
}
