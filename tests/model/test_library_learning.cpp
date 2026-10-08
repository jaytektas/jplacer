// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// What the library learns from boards (DESIGN.md, Matching): a part chosen keeps the names the board's files
// gave it (value and footprint, MPN, supplier PN; its package the footprint), each once, so the next board
// calling it so matches without asking; a part made from a board part (named by MPN, numbered when taken;
// its package found by name or AKA, else made). Matching by what was learned, and what an import takes unasked.
// Tests check with assert(); a Release build must not compile it away.
#undef NDEBUG
#include <cassert>

#include "model/JPLibraryLearning.h"
#include "model/JPPartMatcher.h"

#include <filesystem>

using namespace jf;

int main() {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "jplacer-test-library-learning";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    JPConfiguration config(dir.string());
    auto pkg = std::make_shared<JPPackage>();
    pkg->id = "C0603";
    config.addPackage(pkg);
    auto cap = std::make_shared<JPPart>();
    cap->id = "Cap100n";   // a name the board's files never use
    cap->packageId = "C0603";
    config.addPart(cap);

    JPBoardPart bp;
    bp.fields = { { "value", "100n" }, { "footprint", "C_0603_1608Metric" }, { "mpn", "CL10B104KB8NNNC" },
                  { "manufacturer", "Samsung" }, { "supplierPn", "C14663" }, { "supplier", "LCSC" } };
    // Nothing known by these names yet: no strong candidate, nothing taken unasked.
    assert(!JPPartMatcher::automatic(config, bp));

    JPPart& part = *config.part("Cap100n");
    JPLibraryLearning::learn(config, part, bp, "Ctrl", "2026-10-08T12:00:00");
    JPLibraryLearning::learn(config, part, bp, "Ctrl", "2026-10-08T12:00:01");   // once each
    assert(part.akas.size() == 1 && part.akas[0].field == "valueFootprint" && part.akas[0].text == "100n|C_0603_1608Metric");
    assert(part.akas[0].learnedFrom == "Ctrl" && part.identifiers.size() == 2 && part.identifiers[0].org == "Samsung");
    assert(config.package("C0603")->akas.size() == 1 && config.packageNamed("c_0603_1608metric") == config.package("C0603"));

    // The next board calling it so (by its MPN): taken unasked, and why.
    JPBoardPart next;
    next.fields = { { "value", "100nF" }, { "mpn", "CL10B104KB8NNNC" } };
    assert(JPPartMatcher::automatic(config, next) == &part);
    assert(JPPartMatcher::candidates(config, next).front().why.find("Samsung") != std::string::npos);
    // Both its MPN and its supplier's part number known: the MPN given as why (the stronger).
    JPBoardPart both = bp;
    assert(JPPartMatcher::candidates(config, both).front().why.find("MPN") != std::string::npos);
    // By the value and footprint it learned, with no MPN.
    JPBoardPart byNames;
    byNames.fields = { { "value", "100n" }, { "footprint", "C_0603_1608Metric" } };
    const auto c = JPPartMatcher::candidates(config, byNames);
    assert(!c.empty() && c.front().part == &part && c.front().why.find("learned") != std::string::npos
           && c.front().why.find("Ctrl") != std::string::npos);

    // The MPN from another maker: offered, never taken unasked; the maker's other name is the same maker.
    JPBoardPart otherMaker = next;
    otherMaker.fields["manufacturer"] = "Murata";
    assert(!JPPartMatcher::automatic(config, otherMaker));
    assert(JPPartMatcher::candidates(config, otherMaker).front().why.find("Murata") != std::string::npos);
    config.manufacturers().push_back({ "Samsung Electro-Mechanics", { "Samsung", "SEMCO" } });
    otherMaker.fields["manufacturer"] = "SEMCO";
    assert(JPPartMatcher::automatic(config, otherMaker) == &part);
    assert(config.manufacturerName("semco") == "Samsung Electro-Mechanics" && config.sameManufacturer("Samsung", "SEMCO"));

    // A part made from a board part: by its MPN; its package the library's by AKA.
    JPBoardPart other;
    other.fields = { { "value", "10n" }, { "footprint", "C_0603_1608Metric" }, { "mpn", "CL10B103KB8NNNC" }, { "height", "0.8" },
                     { "description", "10n X7R" } };
    JPPart* made = JPLibraryLearning::addFrom(config, other, "Ctrl", "now");
    assert(made && made->id == "CL10B103KB8NNNC" && made->packageId == "C0603" && made->value == "10n");
    assert(made->name == "10n X7R" && std::abs(made->height.value() - 0.8) < 1e-9 && made->uuid.size() == 36);
    assert(made->identifiers.size() == 1 && made->akas.size() == 1);
    // Its name taken: numbered. A footprint the library has no package for: one made.
    JPBoardPart again = other;
    again.fields["footprint"] = "SOT-23";
    JPPart* second = JPLibraryLearning::addFrom(config, again, "Ctrl", "now");
    assert(second->id == "CL10B103KB8NNNC (2)" && second->packageId == "SOT-23" && config.libraryPackage("SOT-23"));
    std::filesystem::remove_all(dir);
    return 0;
}
