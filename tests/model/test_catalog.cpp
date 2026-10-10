// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// The library's parts and packages (JPCatalog), each with the open boards that use it: a board's matched
// part names its library part (found by uuid when renamed), and so its package; a part to be chosen names none.
// Tests check with assert(); a Release build must not compile it away.
#undef NDEBUG
#include <cassert>

#include "model/JPCatalog.h"

#include <filesystem>

using namespace jf;
namespace fs = std::filesystem;

int main() {
    const fs::path dir = fs::temp_directory_path() / "jplacer-test-catalog";
    fs::remove_all(dir);
    fs::create_directories(dir);
    JPConfiguration config(dir.string());
    for (const char* id : { "C_0603", "LQFP-100" }) {
        auto k = std::make_shared<JPPackage>();
        k->id = id;
        config.addPackage(k);
    }
    auto cap = std::make_shared<JPPart>();
    cap->id = "C_0603-100n";
    cap->packageId = "C_0603";
    config.addPart(cap);
    auto mcu = std::make_shared<JPPart>();
    mcu->id = "LQFP-100-STM32";
    mcu->packageId = "LQFP-100";
    config.addPart(mcu);

    std::string error;
    auto a = config.board((dir / "a.jpboard").string(), error);
    auto b = config.board((dir / "b.jpboard").string(), error);
    assert(a && b);
    // Board a uses the cap (by its id) and the MCU (by its uuid, renamed since); b uses the cap and has one
    // part to be chosen.
    JPBoardPart capA;
    capA.key = "bp-1";
    capA.state = JPBoardPart::State::Matched;
    capA.libraryPartId = cap->id;
    JPBoardPart mcuA;
    mcuA.key = "bp-2";
    mcuA.state = JPBoardPart::State::Matched;
    mcuA.libraryPartId = "an old name";
    mcuA.libraryUuid = mcu->uuid;
    a->parts() = { capA, mcuA };
    JPBoardPart open;
    open.key = "bp-2";
    open.fields["part"] = "SW";
    b->parts() = { capA, open };

    const auto parts = JPCatalog::parts(config);
    assert(parts.size() == 2 && parts[0].part() == cap.get() && parts[1].part() == mcu.get());
    assert(parts[0].usedBy.size() == 2 && parts[1].usedBy.size() == 1 && parts[1].usedBy[0] == a);
    assert(JPCatalog::boardNames(parts[0].usedBy) == "a, b" && JPCatalog::uses(parts[0].usedBy, "b"));
    assert(!JPCatalog::uses(parts[1].usedBy, "b") && JPCatalog::boardNames({}).empty());

    const auto packages = JPCatalog::packages(config);
    assert(packages.size() == 2 && packages[0].usedBy.size() == 2 && packages[1].usedBy.size() == 1);

    fs::remove_all(dir);
    return 0;
}
