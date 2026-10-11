// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPFeederPackaging.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

inline namespace jf {

namespace {

constexpr auto kMm = JPLengthUnit::Millimeters;
constexpr double kSame = 1e-6;   // two values the same

// Where a kind keeps each value: an empty name, not kept.
struct Takes {
    bool        rotationInLocation = false;   // its location's rotation (a strip's Rotation In Tape)
    std::string rotationReal;                 // an attribute in degrees
    std::string rotationChild;                // a child element's whole degrees (a Neoden4's)
    std::string widthLength;                  // a length element (mm)
    std::string pitchLength;                  // a length element (mm)
    std::string pitchNumber;                  // a whole-mm attribute (a Photon's, a Rapid's)
};

Takes takesOf(const std::string& kind) {
    Takes t;
    if (kind == "ReferenceStripFeeder") {
        t.rotationInLocation = true;
        t.widthLength = "tape-width";
        t.pitchLength = "part-pitch";
    } else if (kind == "ReferencePushPullFeeder" || kind == "BambooFeederAutoVision") {
        t.rotationReal = "rotation-in-feeder";
        t.pitchLength = "part-pitch";
    } else if (kind == "BlindsFeeder") {
        t.rotationInLocation = true;
        t.pitchLength = "pocket-pitch";
    } else if (kind == "Neoden4Feeder") {
        t.rotationChild = "part-rotation-in-tape";
        t.pitchLength = "part-pitch-in-tape";
    } else if (kind == "ReferenceDragFeeder" || kind == "ReferenceLeverFeeder") {
        t.pitchLength = "part-pitch";
    } else if (kind == "PhotonFeeder") {
        t.pitchNumber = "part-pitch";
    } else if (kind == "RapidFeeder") {
        t.pitchNumber = "pitch";
    }
    return t;
}

bool tape(const JPPart::Packaging& k) { return k.kind == "Cut tape" || k.kind == "Reel"; }

double mm(const JPLength& l) { return l.convertToUnits(kMm).value(); }

std::string number(double v) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%g", v);
    return buf;
}

} // namespace

bool JPFeederPackaging::takes(const JPFeeder& feeder) {
    const Takes t = takesOf(feeder.typeName());
    return t.rotationInLocation || !t.rotationReal.empty() || !t.rotationChild.empty() || !t.widthLength.empty()
           || !t.pitchLength.empty() || !t.pitchNumber.empty();
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
    const Takes t = takesOf(feeder.typeName());
    auto same = [](double a, double b) { return std::fabs(a - b) <= kSame * std::max(1.0, std::fabs(b)); };
    for (size_t i = 0; i < part.packagings.size(); ++i) {
        const JPPart::Packaging& k = part.packagings[i];
        bool all = true;
        if (t.rotationInLocation) all = all && same(feeder.location().rotation(), k.rotationDeg);
        if (!t.rotationReal.empty()) all = all && same(feeder.real(t.rotationReal, 0), k.rotationDeg);
        if (!t.rotationChild.empty())
            all = all && same(std::atof(feeder.childText(t.rotationChild, "0").c_str()), std::round(k.rotationDeg));
        if (tape(k)) {
            if (!t.widthLength.empty()) all = all && same(mm(feeder.lengthOf(t.widthLength, JPLength(8, kMm))), k.tapeWidthMm);
            if (!t.pitchLength.empty()) all = all && same(mm(feeder.lengthOf(t.pitchLength, JPLength(4, kMm))), k.pitchMm);
            if (!t.pitchNumber.empty()) all = all && same(feeder.number(t.pitchNumber, 4), std::round(k.pitchMm));
        }
        if (all) return int(i);
    }
    return -1;
}

std::string JPFeederPackaging::apply(JPFeeder& feeder, const JPPart::Packaging& k) {
    const Takes t = takesOf(feeder.typeName());
    std::vector<std::string> said;
    if (t.rotationInLocation) {
        feeder.setLocation(feeder.location().derive(std::nullopt, std::nullopt, std::nullopt, k.rotationDeg));
        said.push_back("rotation " + number(k.rotationDeg) + "°");
    } else if (!t.rotationReal.empty()) {
        feeder.setReal(t.rotationReal, k.rotationDeg);
        said.push_back("rotation " + number(k.rotationDeg) + "°");
    } else if (!t.rotationChild.empty()) {
        feeder.setChildText(t.rotationChild, std::to_string(int(std::lround(k.rotationDeg))));
        said.push_back("rotation " + std::to_string(int(std::lround(k.rotationDeg))) + "°");
    }
    // A tape's width and pitch; a tray, tube or loose part has none to give.
    if (tape(k)) {
        if (!t.widthLength.empty()) {
            feeder.setLengthOf(t.widthLength, JPLength(k.tapeWidthMm, kMm));
            said.push_back(number(k.tapeWidthMm) + " mm tape");
        }
        if (!t.pitchLength.empty()) {
            feeder.setLengthOf(t.pitchLength, JPLength(k.pitchMm, kMm));
            said.push_back(number(k.pitchMm) + " mm pitch");
        } else if (!t.pitchNumber.empty()) {
            feeder.setNumber(t.pitchNumber, int(std::lround(k.pitchMm)));
            said.push_back(std::to_string(int(std::lround(k.pitchMm))) + " mm pitch");
        }
    }
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
