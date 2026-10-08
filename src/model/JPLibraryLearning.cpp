// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPLibraryLearning.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>

inline namespace jf {

namespace {

std::string upper(std::string s) {
    for (char& c : s) c = char(std::toupper(uint8_t(c)));
    return s;
}

bool same(const std::string& a, const std::string& b) { return upper(a) == upper(b); }

std::string footprintOf(const JPBoardPart& bp) { return !bp.field("footprint").empty() ? bp.field("footprint") : bp.field("package"); }

void addIdentifier(JPPart& p, const std::string& kind, const std::string& org, const std::string& code) {
    if (code.empty()) return;
    for (const auto& i : p.identifiers)
        if (i.kind == kind && same(i.code, code)) return;
    p.identifiers.push_back({ kind, org, code });
}

} // namespace

void JPLibraryLearning::learn(JPConfiguration& config, JPPart& part, const JPBoardPart& bp, const std::string& from,
                              const std::string& when) {
    const std::string value = bp.field("value"), footprint = footprintOf(bp);
    if (!value.empty() || !footprint.empty()) {
        const std::string field = !value.empty() && !footprint.empty() ? "valueFootprint" : !value.empty() ? "value" : "footprint";
        const std::string text = field == "valueFootprint" ? value + "|" + footprint : !value.empty() ? value : footprint;
        const bool known = std::any_of(part.akas.begin(), part.akas.end(),
                                       [&](const JPPart::Aka& a) { return a.field == field && same(a.text, text); });
        if (!known) part.akas.push_back({ field, text, from, when });
    }
    addIdentifier(part, "mpn", bp.field("manufacturer"), bp.field("mpn"));
    addIdentifier(part, "supplierPn", bp.field("supplier"), bp.field("supplierPn"));
    // Its package known by this footprint too.
    if (JPPackage* k = config.libraryPackage(part.packageId); k && !footprint.empty() && !same(k->id, footprint)
        && std::none_of(k->akas.begin(), k->akas.end(), [&](const std::string& a) { return same(a, footprint); }))
        k->akas.push_back(footprint);
}

JPPart* JPLibraryLearning::addFrom(JPConfiguration& config, const JPBoardPart& bp, const std::string& from,
                                   const std::string& when) {
    const std::string value = bp.field("value"), footprint = footprintOf(bp), mpn = bp.field("mpn");
    std::string base = !mpn.empty() ? mpn : !footprint.empty() && !value.empty() ? footprint + "-" + value
                     : !value.empty() ? value : !footprint.empty() ? footprint : bp.field("part");
    if (base.empty()) base = "Part";
    std::string id = base;
    for (int n = 2; config.libraryPart(id); ++n) id = base + " (" + std::to_string(n) + ")";
    auto p = std::make_shared<JPPart>();
    p->id = id;
    p->value = value;
    if (!bp.field("description").empty()) p->name = bp.field("description");   // OpenPnP's part name is its description
    p->datasheet = bp.field("datasheet");
    if (const std::string h = bp.field("height"); !h.empty()) {
        char* end = nullptr;
        const double mm = std::strtod(h.c_str(), &end);
        if (end != h.c_str() && mm > 0) p->height = JPLength(mm, JPLengthUnit::Millimeters);
    }
    if (JPPackage* k = config.packageNamed(footprint)) {
        p->packageId = k->id;
    } else if (!footprint.empty()) {
        auto k = std::make_shared<JPPackage>();
        k->id = footprint;
        config.addPackage(k);
        p->packageId = footprint;
    }
    config.addPart(p);
    JPPart* made = config.libraryPart(id);
    learn(config, *made, bp, from, when);
    return made;
}

} // inline namespace jf
