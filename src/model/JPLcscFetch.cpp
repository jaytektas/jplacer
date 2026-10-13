// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPLcscFetch.h"

#include "JPConfiguration.h"
#include "JPLcscLookup.h"

#include <algorithm>

inline namespace jf {

namespace {

bool wantsPads(const JPPackage& k) { return k.footprint.pads.empty(); }
bool wantsDimensions(const JPPackage& k) {
    return (k.footprint.bodyWidth <= 0 && k.footprint.bodyHeight <= 0) || k.height.value() <= 0;
}

} // namespace

std::vector<JPLcscFetch::Ask> JPLcscFetch::asksFor(const JPConfiguration& config, const std::vector<std::string>& packageIds,
                                                   bool pads, bool dimensions, int& without) {
    std::vector<Ask> out;
    without = 0;
    for (const auto& k : config.packages()) {
        if (!packageIds.empty() && std::find(packageIds.begin(), packageIds.end(), k->id) == packageIds.end()) continue;
        Ask a;
        a.packageId = k->id;
        a.packageName = k->shownName();
        a.pads = pads && wantsPads(*k);
        a.dimensions = dimensions && wantsDimensions(*k);
        if (!a.pads && !a.dimensions) continue;
        for (const auto& p : config.parts())
            if (p->packageId == k->id)
                if (a.lcsc = JPLcscLookup::lcscOf(*p); !a.lcsc.empty()) break;
        if (a.lcsc.empty()) ++without;
        else out.push_back(a);
    }
    return out;
}

JPLcscFetch::Answer JPLcscFetch::fetch(const std::string& cacheDir, const Ask& ask) {
    Answer a;
    std::string why;
    a.found = JPLcscLookup::find(cacheDir, ask.lcsc, a.part, why, &a.partKept);
    if (!a.found) {
        // Said beside its part number already: not again in front of why.
        const std::string said = JPEasyEdaPart::partNumber(ask.lcsc) + ": ";
        a.problems.push_back(why.rfind(said, 0) == 0 ? why.substr(said.size()) : why);
        return a;
    }
    if (ask.dimensions) {
        double h = 0;
        if (JPLcscLookup::modelHeight(cacheDir, a.part, h, why, &a.modelKept)) a.heightMm = h;
        else a.problems.push_back(why);
    }
    return a;
}

std::string JPLcscFetch::give(JPConfiguration& config, const Ask& ask, const Answer& answer) {
    JPPackage* k = config.package(ask.packageId);
    if (!k || !answer.found) return "";
    std::vector<std::string> given;
    if (ask.pads && k->footprint.pads.empty()) {   // given pads meanwhile: kept
        JPLcscLookup::giveFootprint(*k, answer.part);
        given.push_back(std::to_string(answer.part.pads.size()) + " pads");
        // Its parts' empty fields: what LCSC says of the part.
        for (const auto& p : config.parts())
            if (p->packageId == ask.packageId && JPLcscLookup::lcscOf(*p) == answer.part.lcsc)
                JPLcscLookup::describe(*p, answer.part);
    }
    if (ask.dimensions) {
        const bool body = k->footprint.bodyWidth <= 0 && k->footprint.bodyHeight <= 0;
        const bool height = k->height.value() <= 0;
        if (JPLcscLookup::giveDimensions(*k, answer.part, answer.heightMm)) {
            if (body && k->footprint.bodyWidth > 0) given.push_back("body");
            if (height && k->height.value() > 0) given.push_back("height");
        }
    }
    std::string s;
    for (const std::string& g : given) s += (s.empty() ? "" : ", ") + g;
    return s;
}

} // inline namespace jf
