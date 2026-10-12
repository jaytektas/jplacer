// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPBoardPart.h"
#include "JPConfiguration.h"

#include <string>
#include <vector>

inline namespace jf {

// The library's parts that may be a board part (DESIGN.md, Matching), best
// evidence first, each saying why: its MPN, the Name a file gives it, its supplier's part number,
// OpenPnP's footprint-value name, its value; then the same value written
// another way (100n = 100nF = 0.1µF, 4k7 = 4.7k) of the same size (0603).
// A part whose value is another value is never offered by its footprint.
class JPPartMatcher {
public:
    // A candidate this good is strong enough to take without asking (Use Best Matches): every evidence but
    // the value alone.
    static constexpr int kStrong = 50;
    // A candidate this good is taken as a board comes in, unasked: an identifier, a learned name, OpenPnP's
    // footprint-value name, a learned value in its package.
    static constexpr int kAutomatic = 75;
    // The candidate taken as a board comes in (kAutomatic or better, and no other as strong), or null.
    static JPPart* automatic(const JPConfiguration& config, const JPBoardPart& bp);

    struct Candidate {
        JPPart*       part = nullptr;
        int           score = 0;   // higher, stronger
        std::string   why;
    };

    static std::vector<Candidate> candidates(const JPConfiguration& config, const JPBoardPart& bp);
    // A typed filter's words; a part matches when every word is in its id, Name, description, package or
    // package's description (any case).
    static std::vector<std::string> words(const std::string& filter);
    static bool matches(const JPConfiguration& config, const JPPart& part, const std::vector<std::string>& words);
    // How well a part answers what is typed where one is chosen (a feeder's part): every word (any case)
    // beginning a word of its Name, id, description, value, package, package's description, identifiers or learned names
    // (where letters turn to digits begins one: C0603 holds 0603); a value the same
    // value written another way ("100n" finds 0.1u); "cap", "res", "ind", "led" or "fid" a part whose id or
    // package is named so (C_0603, R0402, LED_0805, Fiducial). 0 when its id (or its name after a library's
    // "Package_QFP:") starts with a word, a word is its value, or nothing is typed; 1 when it only holds them; -1 when it does not
    // answer.
    static int searchRank(const JPConfiguration& config, const JPPart& part, const std::string& typed);
    // The same for a package: by its id and description, a kind by its id.
    static int packageSearchRank(const JPPackage& package, const std::string& typed);
    // A component value as a number: SI prefixes (p n u µ m k M G), the prefix as the decimal point ("4k7",
    // "4R7"), a unit after it ignored (F, H, Ω, R, ohm, V); false when it is not one.
    static bool valueOf(const std::string& text, double& v);
    // An imperial chip size a name holds ("C0603", "R_0603_1608Metric": "0603"); empty when none.
    static std::string chipSize(const std::string& name);
};

} // inline namespace jf
