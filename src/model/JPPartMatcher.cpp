// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPPartMatcher.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

inline namespace jf {

namespace {

constexpr int kMpn = 100, kNamed = 95, kSupplierPn = 90, kMpnOtherMaker = 70, kLearnedBoth = 85, kFootprintValue = 80, kLearnedValue = 75, kValueName = 60,
              kValueAndPackage = 55, kValueAndSize = 50, kValueOnly = 30;
constexpr double kSameValue = 1e-6;   // relative: two values the same

std::string upper(std::string s) {
    for (char& c : s) c = char(std::toupper(uint8_t(c)));
    return s;
}

bool same(const std::string& a, const std::string& b) { return !a.empty() && upper(a) == upper(b); }

// A library part's value as its name holds it: after the last '-' of OpenPnP's footprint-value, else all.
std::string valuePart(const std::string& id) {
    const size_t dash = id.rfind('-');
    return dash == std::string::npos ? id : id.substr(dash + 1);
}

bool sameValue(double a, double b) { return std::abs(a - b) <= kSameValue * std::max(std::abs(a), std::abs(b)); }

// The kinds a word names, by how IPC-7351 (and KiCad's libraries after it) begin a footprint's name.
const std::vector<std::pair<const char*, const char*>> kKinds {
    { "CAP", "C" }, { "RES", "R" }, { "IND", "L" }, { "LED", "LED" }, { "FID", "FID" },
};

// Whether `name` (upper case) is of the kind `prefix` begins: the prefix, then a digit or '_' (or anything,
// for one of three letters or more: LED, FIDUCIAL).
bool ofKind(const std::string& name, const std::string& prefix) {
    if (name.rfind(prefix, 0) != 0) return false;
    if (prefix.size() >= 3) return true;
    return name.size() > prefix.size() && (std::isdigit(uint8_t(name[prefix.size()])) || name[prefix.size()] == '_');
}

} // namespace

bool JPPartMatcher::valueOf(const std::string& text, double& v) {
    std::string t;
    for (const char c : text)
        if (c != ' ') t += c;
    // µ (UTF-8 C2 B5 or the Greek mu CE BC) as u.
    for (const std::string mu : { std::string("\xC2\xB5"), std::string("\xCE\xBC") })
        for (size_t at; (at = t.find(mu)) != std::string::npos;) t.replace(at, mu.size(), "u");
    if (t.empty() || !(std::isdigit(uint8_t(t[0])) || t[0] == '.')) return false;
    size_t i = 0;
    std::string number;
    while (i < t.size() && (std::isdigit(uint8_t(t[i])) || t[i] == '.' || t[i] == ',')) number += t[i] == ',' ? '.' : t[i], ++i;
    double scale = 1;
    if (i < t.size()) {
        const char p = t[i];
        const std::string rest = upper(t.substr(i));
        bool prefix = true;
        switch (p) {
            case 'p': scale = 1e-12; break;
            case 'n': scale = 1e-9; break;
            case 'u': case 'U': scale = 1e-6; break;
            case 'm': scale = 1e-3; break;
            case 'k': case 'K': scale = 1e3; break;
            case 'M': scale = 1e6; break;   // M and MEG
            case 'G': scale = 1e9; break;
            case 'R': case 'r': scale = 1; break;   // 4R7
            default: prefix = false; break;
        }
        if (prefix) {
            ++i;
            if (rest.rfind("MEG", 0) == 0) i += 2;
            // The prefix as the decimal point: digits after it are the fraction ("4k7").
            std::string fraction;
            while (i < t.size() && std::isdigit(uint8_t(t[i]))) fraction += t[i++];
            if (!fraction.empty()) {
                if (number.find('.') != std::string::npos) return false;
                number += "." + fraction;
            }
        }
        // What is left is a unit, or not a value at all.
        const std::string unit = upper(t.substr(i));
        static const char* units[] = { "", "F", "H", "R", "OHM", "OHMS", "V", "A", "HZ", "W", "\xCE\xA9", "\xE2\x84\xA6" };
        if (std::none_of(std::begin(units), std::end(units), [&unit](const char* u) { return unit == u; })) return false;
    }
    if (number.empty() || number == ".") return false;
    v = std::strtod(number.c_str(), nullptr) * scale;
    return true;
}

std::string JPPartMatcher::chipSize(const std::string& name) {
    static const char* sizes[] = { "01005", "0201", "0402", "0603", "0805", "1206", "1210", "1812", "2010", "2512" };
    for (const char* s : sizes) {
        const size_t at = name.find(s);
        if (at == std::string::npos) continue;
        // Its own run of digits, not part of a longer number ("0603" in "1608" is not).
        const size_t end = at + std::string(s).size();
        if ((at == 0 || !std::isdigit(uint8_t(name[at - 1]))) && (end == name.size() || !std::isdigit(uint8_t(name[end])))) return s;
    }
    return "";
}

std::vector<JPPartMatcher::Candidate> JPPartMatcher::candidates(const JPConfiguration& config, const JPBoardPart& bp) {
    const std::string value = bp.field("value"), mpn = bp.field("mpn"), supplierPn = bp.field("supplierPn");
    const std::string named = bp.field("name");   // the Name a file gives it
    const std::string footprint = !bp.field("footprint").empty() ? bp.field("footprint") : bp.field("package");
    const std::string size = chipSize(footprint);
    const JPPackage* package = config.packageNamed(footprint);   // the library's package the footprint names
    double v = 0;
    const bool hasValue = valueOf(value, v);
    std::vector<Candidate> out;
    for (const auto& p : config.parts()) {
        Candidate c;
        c.part = p.get();
        const std::string name = p->name.value_or("");
        // What it is called: its Name (a new part's id is a GUID, and says nothing), else its id.
        const std::string& called = p->shownName();
        auto calledSo = [&](const std::string& text) { return same(p->id, text) || same(called, text); };
        // What the library knows the part by: its identifiers, and the names it learned.
        const JPPart::Identifier* byId = nullptr;
        for (const auto& i : p->identifiers) {
            if (i.kind == "mpn" && same(i.code, mpn)) {
                byId = &i;   // the strongest: no need to look further
                break;
            }
            if (i.kind == "supplierPn" && same(i.code, supplierPn) && !byId) byId = &i;
        }
        const JPPart::Aka* learned = nullptr;
        const JPPart::Aka* learnedValue = nullptr;
        for (const auto& a : p->akas) {
            if (a.field == "valueFootprint" && !value.empty() && !footprint.empty() && same(a.text, value + "|" + footprint)) learned = &a;
            if (a.field == "value" && same(a.text, value)) learnedValue = &a;
        }
        const bool samePackage = package && same(package->id, p->packageId);
        const std::string maker = bp.field("manufacturer");
        if (byId && byId->kind == "mpn" && !maker.empty() && !byId->org.empty() && !config.sameManufacturer(maker, byId->org)) {
            // The same code from another maker: a candidate to look at, never taken unasked.
            c.score = kMpnOtherMaker;
            c.why = "its MPN " + byId->code + ", but made by " + config.manufacturerName(byId->org) + " (the file says " + maker + ")";
        } else if (byId) {
            c.score = byId->kind == "mpn" ? kMpn : kSupplierPn;
            c.why = (byId->kind == "mpn" ? "its MPN " : "its supplier's part number ") + byId->code
                  + (byId->org.empty() ? std::string() : " (" + config.manufacturerName(byId->org) + ")");
        } else if (!named.empty() && calledSo(named)) {
            c.score = kNamed;
            c.why = "named " + named + " (as the file names it)";
        } else if (learned) {
            c.score = kLearnedBoth;
            c.why = "learned: value " + value + " and footprint " + footprint
                  + (learned->learnedFrom.empty() ? std::string() : ", from " + learned->learnedFrom);
        } else if (calledSo(mpn) || same(name, mpn)) {
            c.score = kMpn;
            c.why = "its MPN " + mpn;
        } else if (calledSo(supplierPn) || same(name, supplierPn)) {
            c.score = kSupplierPn;
            c.why = "its supplier's part number " + supplierPn;
        } else if (!footprint.empty() && !value.empty() && calledSo(footprint + "-" + value)) {
            c.score = kFootprintValue;
            c.why = "named by its footprint and value";
        } else if (learnedValue && samePackage) {
            c.score = kLearnedValue;
            c.why = "learned: value " + value + ", its package " + p->packageId;
        } else if (calledSo(value)) {
            c.score = kValueName;
            c.why = "named by its value";
        } else if (double pv = 0; hasValue && valueOf(!p->value.empty() ? p->value : valuePart(called), pv) && sameValue(v, pv)) {
            const std::string theirSize = !chipSize(p->packageId).empty() ? chipSize(p->packageId) : chipSize(called);
            if (samePackage) {
                c.score = kValueAndPackage;
                c.why = "value " + (!p->value.empty() ? p->value : valuePart(called)) + " = " + value + ", its package " + p->packageId;
            } else if (!size.empty() && theirSize == size) {
                c.score = kValueAndSize;
                c.why = "value " + (!p->value.empty() ? p->value : valuePart(called)) + " = " + value + ", size " + size;
            } else if (size.empty() || theirSize.empty()) {
                c.score = kValueOnly;
                c.why = "value " + (!p->value.empty() ? p->value : valuePart(called)) + " = " + value;
            }
        }
        if (c.score > 0) out.push_back(c);
    }
    std::stable_sort(out.begin(), out.end(), [](const Candidate& a, const Candidate& b) {
        return a.score != b.score ? a.score > b.score : a.part->id < b.part->id;
    });
    return out;
}

JPPart* JPPartMatcher::automatic(const JPConfiguration& config, const JPBoardPart& bp) {
    const auto c = candidates(config, bp);
    if (c.empty() || c.front().score < kAutomatic) return nullptr;
    // Only when it is the one: another as strong (two parts that learned the same name, a 1 % and a 5 %
    // resistor) is for the user to choose between, not taken by its place in the list.
    if (c.size() > 1 && c[1].score >= c.front().score) return nullptr;
    return c.front().part;
}

std::vector<std::string> JPPartMatcher::words(const std::string& filter) {
    std::vector<std::string> out;
    std::string w;
    for (const char c : filter) {
        if (std::isspace(uint8_t(c))) {
            if (!w.empty()) out.push_back(upper(w));
            w.clear();
        } else {
            w += c;
        }
    }
    if (!w.empty()) out.push_back(upper(w));
    return out;
}

bool JPPartMatcher::matches(const JPConfiguration& config, const JPPart& part, const std::vector<std::string>& words) {
    std::string text = upper(part.id + " " + part.label + " " + part.name.value_or("") + " " + part.packageId);
    if (const JPPackage* k = config.package(part.packageId)) text += " " + upper(k->description.value_or(""));
    return std::all_of(words.begin(), words.end(), [&text](const std::string& w) { return text.find(w) != std::string::npos; });
}

namespace {

// Whether `text` holds `word` (both upper case) where a word of it begins: after a space or punctuation, or
// where letters turn to digits or back (C0603 holds 0603 and 06, being typed; 1608METRIC holds METRIC).
bool holdsWord(const std::string& text, const std::string& word) {
    auto alnum = [](char c) { return std::isalnum(uint8_t(c)) != 0; };
    auto digit = [](char c) { return std::isdigit(uint8_t(c)) != 0; };
    for (size_t at = text.find(word); at != std::string::npos; at = text.find(word, at + 1))
        if (at == 0 || !alnum(text[at - 1]) || digit(text[at - 1]) != digit(word[0])) return true;
    return false;
}

// How well `typed` answers a library entry (searchRank, packageSearchRank): `id` and `text` upper case, `value`
// the part's value (none: NaN), `kindOf` the names its kind is told by.
int rankOf(const std::string& id, const std::string& text, double value, const std::vector<std::string>& kindOf,
           const std::string& typed) {
    bool starts = false, any = false;
    size_t at = 0;
    while ((at = typed.find_first_not_of(" \t", at)) != std::string::npos) {
        const size_t end = std::min(typed.find_first_of(" \t", at), typed.size());
        const std::string word = typed.substr(at, end - at), w = upper(word);   // its case kept for a value (m, M)
        at = end;
        any = true;
        bool found = holdsWord(text, w);
        // Its value, however written, is as good as its name starting so.
        if (double v = 0; !std::isnan(value) && JPPartMatcher::valueOf(word, v) && sameValue(v, value)) found = starts = true;
        for (const auto& [kind, prefix] : kKinds)
            for (const std::string& name : kindOf)
                if (!found && w == kind) found = ofKind(name, prefix);
        if (!found) return -1;
        // Its name starting so: the id's start, or its name's after a library's (Package_QFP:LQFP-100…).
        const size_t colon = id.find(':');
        starts = starts || id.rfind(w, 0) == 0 || (colon != std::string::npos && id.compare(colon + 1, w.size(), w) == 0);
    }
    return starts || !any ? 0 : 1;
}

} // namespace

int JPPartMatcher::searchRank(const JPConfiguration& config, const JPPart& part, const std::string& typed) {
    // Its package by its Name (its id is a GUID).
    const JPPackage* k = config.package(part.packageId);
    const std::string id = upper(part.shownName()), package = k ? upper(k->shownName()) : std::string();
    std::string text = id + " " + upper(part.id) + " " + upper(part.name.value_or("")) + " " + upper(part.value) + " " + package;
    if (k) text += " " + upper(k->description.value_or(""));
    for (const JPPart::Identifier& i : part.identifiers) text += " " + upper(i.code);
    for (const JPPart::Aka& a : part.akas) text += " " + upper(a.text);
    double value = 0;
    if (!valueOf(!part.value.empty() ? part.value : valuePart(part.shownName()), value)) value = std::nan("");
    return rankOf(id, text, value, { id, package }, typed);
}

int JPPartMatcher::packageSearchRank(const JPPackage& package, const std::string& typed) {
    // By its Name (what it is shown and searched by), and its description.
    const std::string id = upper(package.shownName());
    return rankOf(id, id + " " + upper(package.description.value_or("")), std::nan(""), { id }, typed);
}

} // inline namespace jf
