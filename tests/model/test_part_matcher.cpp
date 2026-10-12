// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// The part picker's matching (DESIGN.md, Matching): component values however written (100n = 100nF =
// 0.1uF = 0.1µF, 4k7 = 4.7k, 4R7); chip sizes in names; the library's parts a board part may be, best
// evidence first, each saying why (MPN, supplier PN, footprint-value, value, value and size), a part of
// another value never offered; the filter's words; a placement split from its board part, and a library part
// made from what its files said (Add to Library).
// Tests check with assert(); a Release build must not compile it away.
#undef NDEBUG
#include <cassert>
#include <cmath>

#include "model/JPLibraryLearning.h"
#include "model/JPPartMatcher.h"

#include <filesystem>

using namespace jf;

namespace {

bool value(const std::string& s, double want) {
    double v = 0;
    return JPPartMatcher::valueOf(s, v) && std::abs(v - want) <= 1e-9 * std::max(1.0, std::abs(want));
}

void part(JPConfiguration& c, const std::string& id, const std::string& package) {
    auto p = std::make_shared<JPPart>();
    p->id = id;
    p->packageId = package;
    c.addPart(p);
}

} // namespace

int main() {
    assert(value("100n", 100e-9) && value("100nF", 100e-9) && value("0.1uF", 100e-9) && value("0.1\xC2\xB5" "F", 100e-9));
    assert(value("4k7", 4700) && value("4.7k", 4700) && value("4R7", 4.7) && value("10K", 10000) && value("1M", 1e6));
    assert(value("2.2mH", 2.2e-3) && value("100", 100) && value("47 ohm", 47) && value("1,5k", 1500));
    double v;
    assert(!JPPartMatcher::valueOf("LM358", v) && !JPPartMatcher::valueOf("4k7k", v) && !JPPartMatcher::valueOf("", v));
    assert(JPPartMatcher::chipSize("C_0603_1608Metric") == "0603" && JPPartMatcher::chipSize("R0402") == "0402");
    assert(JPPartMatcher::chipSize("SOIC-8") == "" && JPPartMatcher::chipSize("X106031") == "");

    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "jplacer-test-part-matcher";
    std::filesystem::create_directories(dir);
    JPConfiguration config(dir.string());
    part(config, "CL10B104KB8NNNC", "C0603");
    part(config, "C14663", "C0603");
    part(config, "C0603-100n", "C0603");
    part(config, "C_0603_1608Metric-0.1uF", "C_0603_1608Metric");
    part(config, "C_0805_2012Metric-100nF", "C_0805_2012Metric");
    part(config, "C_0603_1608Metric-10nF", "C_0603_1608Metric");
    part(config, "100n", "");

    JPBoardPart bp;
    bp.fields = { { "value", "100n" }, { "footprint", "C0603" }, { "mpn", "CL10B104KB8NNNC" }, { "supplierPn", "C14663" } };
    const auto c = JPPartMatcher::candidates(config, bp);
    assert(c.size() == 5);
    assert(c[0].part->id == "CL10B104KB8NNNC" && c[0].why.find("MPN") != std::string::npos);
    assert(c[1].part->id == "C14663" && c[2].part->id == "C0603-100n" && c[3].part->id == "100n");
    assert(c[4].part->id == "C_0603_1608Metric-0.1uF" && c[4].why.find("size 0603") != std::string::npos);
    for (const auto& x : c) assert(x.part->id != "C_0603_1608Metric-10nF" && x.part->id != "C_0805_2012Metric-100nF");

    // The filter: every word, in the id, the name or the package.
    const auto words = JPPartMatcher::words("  0603 metric ");
    assert(words.size() == 2 && words[0] == "0603");
    assert(JPPartMatcher::matches(config, *config.part("C_0603_1608Metric-10nF"), words));
    assert(!JPPartMatcher::matches(config, *config.part("C0603-100n"), words));

    // A board: C1, C2 of one part; C1 split off alone; a library part made from what the files said.
    JPBoard board;
    board.file = "/work/Ctrl.jpboard";
    JPBoardPart shared;
    shared.key = "bp-1";
    shared.fields = { { "part", "C0603-47n" }, { "value", "47n" }, { "footprint", "C0603" }, { "height", "0.9" } };
    board.parts().push_back(shared);
    for (const char* id : { "C1", "C2" }) {
        JPPlacement p;
        p.id = id;
        p.boardPart = "bp-1";
        board.placements.push_back(p);
    }
    const std::string own = board.splitPlacement("C1");
    assert(own != "bp-1" && board.part(own)->field("value") == "47n" && board.placementsOf("bp-1").size() == 2);
    board.placements[0].boardPart = own;
    assert(board.splitPlacement("C1") == own);   // alone already: its own
    const JPPart* made = JPLibraryLearning::addFrom(config, *board.part(own), board.scopeName(), "2026-10-11T00:00:00");
    assert(made && config.libraryPart(made->id) == made && made->value == "47n");

    // Searching where a part is chosen (a feeder's Part): every word, in any case, anywhere; a value written
    // another way; a kind by its footprint's prefix; a start first.
    part(config, "LQFP-48_STM32", "LQFP-48");
    part(config, "R_0603_1608Metric-10k", "R_0603_1608Metric");
    auto rank = [&config](const char* id, const char* typed) { return JPPartMatcher::searchRank(config, *config.part(id), typed); };
    assert(rank("LQFP-48_STM32", "lq") == 0 && rank("LQFP-48_STM32", "48") == 1 && rank("LQFP-48_STM32", "0603") == -1);
    assert(rank("C0603-100n", "0603 100n") == 0 && rank("C0603-100n", "0.1u") == 0 && rank("C0603-100n", "0603") == 1 && rank("C0603-100n", "10n") == -1);
    assert(rank("C0603-100n", "cap") == 1 && rank("R_0603_1608Metric-10k", "res 10k") == 0 && rank("C0603-100n", "res") == -1);
    assert(rank("C0603-100n", "") == 0 && rank("C0603-100n", "c06") == 0);
    // A word begins one of the part's (not inside "wordpress").
    part(config, "Package_QFP:LQFP-100_14x14mm", "Package_QFP:LQFP-100_14x14mm");
    part(config, "C1206-22u", "C1206");
    config.part("C1206-22u")->name = "see www.wordpress.com";
    assert(rank("Package_QFP:LQFP-100_14x14mm", "lq") == 0 && rank("Package_QFP:LQFP-100_14x14mm", "14x14") == 1);
    assert(rank("C1206-22u", "res") == -1 && rank("C1206-22u", "206") == -1 && rank("C1206-22u", "1206") == 1);
    assert(rank("C1206-22u", "word") == 1 && rank("LQFP-48_STM32", "stm") == 1);

    // Two parts that learned the same name (a 1 % and a 5 % 10k): both offered, neither taken unasked; a part
    // renamed still found by it.
    {
        auto one = std::make_shared<JPPart>();
        one->id = "guid-1";
        one->label = "10k 0603 1%";
        one->akas.push_back({ "valueFootprint", "10k|R_0603_1608Metric", "b", "now" });
        auto five = std::make_shared<JPPart>();
        five->id = "guid-5";
        five->label = "10k 0603 5%";
        five->akas.push_back({ "valueFootprint", "10k|R_0603_1608Metric", "b", "now" });
        config.addPart(one);
        config.addPart(five);
        JPBoardPart r;
        r.fields = { { "value", "10k" }, { "footprint", "R_0603_1608Metric" } };
        const auto both = JPPartMatcher::candidates(config, r);
        assert(both.size() >= 2 && both[0].score == both[1].score && JPPartMatcher::automatic(config, r) == nullptr);
        config.removePart("guid-5");
        assert(JPPartMatcher::automatic(config, r) == config.part("guid-1"));   // the one: taken
        assert(config.partNamed("10K 0603 1%") == config.part("guid-1") && !config.partNamed("guid-5"));
        config.removePart("guid-1");
    }
    // Named as the file names it, but the file's other evidence another part's (EasyEDA's CPL names an 0402 100nF
    // as it names the library's 0603): offered, not taken unasked; with nothing against it, taken.
    {
        auto k = std::make_shared<JPPackage>();
        k->id = "pkg-c0603-named";
        k->label = "C0603-named";
        config.addPackage(k);
        auto big = std::make_shared<JPPart>();
        big->id = "guid-100n";
        big->label = "100nF";
        big->packageId = k->id;
        big->identifiers.push_back({ "supplierPn", "LCSC", "C14663" });
        config.addPart(big);
        JPBoardPart small;
        small.fields = { { "name", "100nF" }, { "footprint", "C0402" }, { "supplierPn", "C131394" } };
        const auto offered = JPPartMatcher::candidates(config, small);
        assert(!offered.empty() && offered.front().part == big.get() && offered.front().score < JPPartMatcher::kAutomatic);
        assert(offered.front().why.find("C131394") != std::string::npos && !JPPartMatcher::automatic(config, small));
        auto k0402 = std::make_shared<JPPackage>();
        k0402->id = "pkg-c0402-named";
        k0402->label = "C0402-named";
        config.addPackage(k0402);
        JPBoardPart other;   // its footprint another of the library's packages
        other.fields = { { "name", "100nF" }, { "footprint", "C0402-named" } };
        assert(!JPPartMatcher::automatic(config, other) && JPPartMatcher::candidates(config, other).front().why.find("footprint") != std::string::npos);
        JPBoardPart plain;   // nothing against it
        plain.fields = { { "name", "100nF" } };
        assert(JPPartMatcher::automatic(config, plain) == big.get());
        config.removePart("guid-100n");
    }
    std::filesystem::remove_all(dir);
    return 0;
}
