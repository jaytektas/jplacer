// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPLaneChoice.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

inline namespace jf {

namespace {

// The part's tape width in mm (its first tape packaging); 0: not known.
double tapeWidthOf(const JPConfiguration& config, const std::string& partId) {
    const JPPart* part = config.libraryPart(partId);
    if (!part) return 0;
    for (const JPPart::Packaging& k : part->packagings)
        if (k.kind == "Cut tape" || k.kind == "Reel") return k.tapeWidthMm;
    return 0;
}

} // namespace

std::vector<JPLaneChoice::Lane> JPLaneChoice::free(const JPConfiguration& config, const std::string& partId,
                                                   const std::set<std::string>& stillNeeded) {
    const double want = tapeWidthOf(config, partId);
    std::vector<std::pair<int, Lane>> ranked;
    for (const JPFeeder& f : config.feeders()) {
        if (!f.isLane()) continue;
        const std::string holds = f.partId();
        const bool empty = holds.empty() || !f.enabled();
        if (!empty && (holds == partId || stillNeeded.count(holds))) continue;
        Lane l;
        l.feederId = f.id();
        l.name = f.name();
        l.removePartId = holds;
        l.widthMm = f.tapeWidth().convertToUnits(JPLengthUnit::Millimeters).value();
        l.widthFits = want <= 0 || std::abs(l.widthMm - want) < 0.5;
        ranked.emplace_back((empty ? 0 : 2) + (l.widthFits ? 0 : 1), l);
    }
    std::stable_sort(ranked.begin(), ranked.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    std::vector<Lane> out;
    for (auto& [rank, l] : ranked) out.push_back(std::move(l));
    return out;
}

std::string JPLaneChoice::packagingWords(const JPConfiguration& config, const std::string& partId) {
    const JPPart* part = config.libraryPart(partId);
    if (!part || part->packagings.empty()) return "";
    const JPPart::Packaging& k = part->packagings.front();
    std::string s = k.kind;
    char buf[96];
    if (k.kind == "Cut tape" || k.kind == "Reel") {
        std::snprintf(buf, sizeof buf, ", %g mm %s, %g mm pitch", k.tapeWidthMm, k.tapeType.c_str(), k.pitchMm);
        s += buf;
    }
    if (k.rotationDeg != 0) {
        std::snprintf(buf, sizeof buf, ", the part turned %g° in it", k.rotationDeg);
        s += buf;
    }
    return s;
}

} // inline namespace jf
