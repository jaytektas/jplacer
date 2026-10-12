// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPLibraryLearning.h"

#include "common/JPUuid.h"

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

bool JPLibraryLearning::learnName(JPPart& part, const JPBoardPart& bp, const std::string& from, const std::string& when) {
    const std::string value = bp.field("value"), footprint = footprintOf(bp);
    if (value.empty() && footprint.empty()) return false;
    const std::string field = !value.empty() && !footprint.empty() ? "valueFootprint" : !value.empty() ? "value" : "footprint";
    const std::string text = field == "valueFootprint" ? value + "|" + footprint : !value.empty() ? value : footprint;
    const bool known = std::any_of(part.akas.begin(), part.akas.end(),
                                   [&](const JPPart::Aka& a) { return a.field == field && same(a.text, text); });
    if (known) return false;
    part.akas.push_back({ field, text, from, when });
    return true;
}

void JPLibraryLearning::describe(JPPart& part, const JPBoardPart& bp) {
    if (!part.name && !bp.field("description").empty()) part.name = bp.field("description");   // OpenPnP's part name is its description
    if (part.datasheet.empty()) part.datasheet = bp.field("datasheet");
    addIdentifier(part, "mpn", bp.field("manufacturer"), bp.field("mpn"));
    addIdentifier(part, "supplierPn", bp.field("supplier"), bp.field("supplierPn"));
}

void JPLibraryLearning::learn(JPConfiguration& config, JPPart& part, const JPBoardPart& bp, const std::string& from,
                              const std::string& when) {
    const std::string footprint = footprintOf(bp);
    learnName(part, bp, from, when);
    addIdentifier(part, "mpn", bp.field("manufacturer"), bp.field("mpn"));
    addIdentifier(part, "supplierPn", bp.field("supplier"), bp.field("supplierPn"));
    // Its package's footprint known by this name too (unless a footprint, of any package, has it already).
    const JPPackage* package = config.libraryPackage(part.packageId);
    if (!footprint.empty() && package && !same(package->shownName(), footprint) && !config.footprintNamed(footprint))
        if (JPLibraryFootprint* f = config.defaultFootprint(part.packageId, true)) f->cadNames.push_back(footprint);
}

JPPart* JPLibraryLearning::addFrom(JPConfiguration& config, const JPBoardPart& bp, const std::string& from,
                                   const std::string& when) {
    const std::string value = bp.field("value"), footprint = footprintOf(bp), mpn = bp.field("mpn");
    std::string base = !mpn.empty() ? mpn : !footprint.empty() && !value.empty() ? footprint + "-" + value
                     : !value.empty() ? value : !footprint.empty() ? footprint : bp.field("part");
    if (base.empty()) base = "Part";
    // Named so (numbered when the name is taken); known for good by a GUID, so the name can be changed.
    std::string name = base;
    for (int n = 2; config.partNamed(name) || config.libraryPart(name); ++n) name = base + " (" + std::to_string(n) + ")";
    const std::string id = JPUuid::make();
    auto p = std::make_shared<JPPart>();
    p->id = id;
    p->label = name;
    p->value = value;
    describe(*p, bp);
    if (const std::string h = bp.field("height"); !h.empty()) {
        char* end = nullptr;
        const double mm = std::strtod(h.c_str(), &end);
        if (end != h.c_str() && mm > 0) p->height = JPLength(mm, JPLengthUnit::Millimeters);
    }
    if (JPPackage* k = config.packageNamed(footprint)) {
        p->packageId = k->id;
    } else if (!footprint.empty()) {
        // A package made, named so (its GUID made as it is added).
        auto k = std::make_shared<JPPackage>();
        k->label = footprint;
        config.addPackage(k);
        p->packageId = k->id;
    }
    config.addPart(p);
    JPPart* made = config.libraryPart(id);
    learn(config, *made, bp, from, when);
    return made;
}

} // inline namespace jf
