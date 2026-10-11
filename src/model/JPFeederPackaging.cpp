// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPFeederPackaging.h"

#include <cmath>
#include <cstdio>

inline namespace jf {

namespace {

constexpr double kSame = 1e-6;   // two values the same

bool tape(const JPPart::Packaging& k) { return k.kind == "Cut tape" || k.kind == "Reel"; }

// What `k` gives a feeder: its rotation, and a tape's width and pitch.
JPFeeder::Tape given(const JPPart::Packaging& k) {
    JPFeeder::Tape t;
    t.rotationDeg = k.rotationDeg;
    if (tape(k)) {
        t.widthMm = k.tapeWidthMm;
        t.pitchMm = k.pitchMm;
    }
    return t;
}

std::string number(double v) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%g", v);
    return buf;
}

} // namespace

bool JPFeederPackaging::takes(const JPFeeder& feeder) {
    const JPFeeder::Tape t = feeder.tape();
    return t.rotationDeg || t.widthMm || t.pitchMm;
}

std::vector<std::string> JPFeederPackaging::labels(const JPPart& part) {
    std::vector<std::string> out;
    for (size_t i = 0; i < part.packagings.size(); ++i) {
        const JPPart::Packaging& k = part.packagings[i];
        std::string s = std::to_string(i + 1) + ": " + k.kind;
        if (tape(k)) s += ", " + number(k.tapeWidthMm) + " mm, " + number(k.pitchMm) + " mm pitch";
        s += ", " + number(k.rotationDeg) + "°";
        out.push_back(s);
    }
    return out;
}

int JPFeederPackaging::matching(const JPFeeder& feeder, const JPPart& part) {
    auto same = [](const std::optional<double>& a, const std::optional<double>& b) {
        return !b || (a && std::fabs(*a - *b) <= kSame * std::max(1.0, std::fabs(*b)));
    };
    const JPFeeder::Tape has = feeder.tape();
    for (size_t i = 0; i < part.packagings.size(); ++i) {
        const JPFeeder::Tape want = feeder.kept(given(part.packagings[i]));
        if (same(has.rotationDeg, want.rotationDeg) && same(has.widthMm, want.widthMm) && same(has.pitchMm, want.pitchMm))
            return int(i);
    }
    return -1;
}

std::string JPFeederPackaging::apply(JPFeeder& feeder, const JPPart::Packaging& k) {
    const JPFeeder::Tape kept = feeder.kept(given(k));
    feeder.setTape(kept);
    // A tape's width and pitch; a tray, tube or loose part has none to give.
    std::vector<std::string> said;
    if (kept.rotationDeg) said.push_back("rotation " + number(*kept.rotationDeg) + "°");
    if (kept.widthMm) said.push_back(number(*kept.widthMm) + " mm tape");
    if (kept.pitchMm) said.push_back(number(*kept.pitchMm) + " mm pitch");
    std::string out;
    for (const std::string& s : said) out += (out.empty() ? "" : ", ") + s;
    return out;
}

int JPFeederPackaging::forLoading(const JPPart& part) {
    if (part.packagings.size() == 1) return 0;
    if (part.lastPackaging >= 0 && size_t(part.lastPackaging) < part.packagings.size()) return part.lastPackaging;
    return -1;
}

std::string JPFeederPackaging::load(JPConfiguration& config, JPFeeder& feeder, const std::string& partId) {
    if (partId == feeder.partId()) return {};   // the same part again: the feeder's values as they are
    feeder.setPartId(partId);
    const JPPart* part = config.part(partId);
    const int i = part ? forLoading(*part) : -1;
    return i >= 0 ? apply(feeder, part->packagings[size_t(i)]) : std::string();
}

std::string JPFeederPackaging::choose(JPConfiguration& config, JPFeeder& feeder, int index) {
    JPPart* part = config.part(feeder.partId());
    if (!part || index < 0 || size_t(index) >= part->packagings.size()) return {};
    part->lastPackaging = index;
    return apply(feeder, part->packagings[size_t(index)]);
}

} // inline namespace jf
