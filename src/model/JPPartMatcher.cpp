// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPPartMatcher.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>

inline namespace jf {

namespace {

constexpr int kMpn = 100, kSupplierPn = 90, kLearnedBoth = 85, kFootprintValue = 80, kLearnedValue = 75, kValueName = 60,
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
        if (byId) {
            c.score = byId->kind == "mpn" ? kMpn : kSupplierPn;
            c.why = (byId->kind == "mpn" ? "its MPN " : "its supplier's part number ") + byId->code
                  + (byId->org.empty() ? std::string() : " (" + byId->org + ")");
        } else if (learned) {
            c.score = kLearnedBoth;
            c.why = "learned: value " + value + " and footprint " + footprint
                  + (learned->learnedFrom.empty() ? std::string() : ", from " + learned->learnedFrom);
        } else if (same(p->id, mpn) || same(name, mpn)) {
            c.score = kMpn;
            c.why = "its MPN " + mpn;
        } else if (same(p->id, supplierPn) || same(name, supplierPn)) {
            c.score = kSupplierPn;
            c.why = "its supplier's part number " + supplierPn;
        } else if (!footprint.empty() && !value.empty() && same(p->id, footprint + "-" + value)) {
            c.score = kFootprintValue;
            c.why = "named by its footprint and value";
        } else if (learnedValue && samePackage) {
            c.score = kLearnedValue;
            c.why = "learned: value " + value + ", its package " + p->packageId;
        } else if (same(p->id, value)) {
            c.score = kValueName;
            c.why = "named by its value";
        } else if (double pv = 0; hasValue && valueOf(!p->value.empty() ? p->value : valuePart(p->id), pv) && sameValue(v, pv)) {
            const std::string theirSize = !chipSize(p->packageId).empty() ? chipSize(p->packageId) : chipSize(p->id);
            if (samePackage) {
                c.score = kValueAndPackage;
                c.why = "value " + (!p->value.empty() ? p->value : valuePart(p->id)) + " = " + value + ", its package " + p->packageId;
            } else if (!size.empty() && theirSize == size) {
                c.score = kValueAndSize;
                c.why = "value " + (!p->value.empty() ? p->value : valuePart(p->id)) + " = " + value + ", size " + size;
            } else if (size.empty() || theirSize.empty()) {
                c.score = kValueOnly;
                c.why = "value " + (!p->value.empty() ? p->value : valuePart(p->id)) + " = " + value;
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
    return !c.empty() && c.front().score >= kAutomatic ? c.front().part : nullptr;
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
    std::string text = upper(part.id + " " + part.name.value_or("") + " " + part.packageId);
    if (const JPPackage* k = config.package(part.packageId)) text += " " + upper(k->description.value_or(""));
    return std::all_of(words.begin(), words.end(), [&text](const std::string& w) { return text.find(w) != std::string::npos; });
}

} // inline namespace jf
