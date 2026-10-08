// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPBoardPart.h"
#include "JPConfiguration.h"

#include <string>
#include <vector>

inline namespace jf {

// The library's parts that may be a board part (DESIGN.md, Matching), best
// evidence first, each saying why: its MPN, its supplier's part number,
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
    // The candidate taken as a board comes in (kAutomatic or better), or null.
    static JPPart* automatic(const JPConfiguration& config, const JPBoardPart& bp);

    struct Candidate {
        JPPart*       part = nullptr;
        int           score = 0;   // higher, stronger
        std::string   why;
    };

    static std::vector<Candidate> candidates(const JPConfiguration& config, const JPBoardPart& bp);
    // A typed filter's words; a part matches when every word is in its id, name, package or package's
    // description (any case).
    static std::vector<std::string> words(const std::string& filter);
    static bool matches(const JPConfiguration& config, const JPPart& part, const std::vector<std::string>& words);
    // A component value as a number: SI prefixes (p n u µ m k M G), the prefix as the decimal point ("4k7",
    // "4R7"), a unit after it ignored (F, H, Ω, R, ohm, V); false when it is not one.
    static bool valueOf(const std::string& text, double& v);
    // An imperial chip size a name holds ("C0603", "R_0603_1608Metric": "0603"); empty when none.
    static std::string chipSize(const std::string& name);
};

} // inline namespace jf
