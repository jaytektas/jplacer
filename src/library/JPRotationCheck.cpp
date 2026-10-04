// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPRotationCheck.h"

#include <cctype>
#include <cmath>
#include <map>

inline namespace jf {

namespace {

constexpr double kPi = 3.14159265358979323846;
// Pads this close (mm) are the same place; a pad 1 within this angle (deg) of
// where the package puts it agrees.
constexpr double kSameMm = 0.02;
constexpr double kAgreeDeg = 20;

// A designator's letters: "R12" -> "R", "FB3" -> "FB".
std::string prefix(const std::string& d) {
    std::string out;
    for (const char c : d) {
        if (!std::isalpha(static_cast<unsigned char>(c))) break;
        out += char(std::toupper(static_cast<unsigned char>(c)));
    }
    return out;
}

bool unpolarised(const std::string& designator) {
    const std::string p = prefix(designator);
    return p == "R" || p == "C" || p == "L" || p == "FB";
}

} // namespace

bool JPRotationCheck::symmetric(const JPFootprint& f, int quarters) {
    const double a = quarters * kPi / 2, c = std::cos(a), s = std::sin(a);
    const bool swap = quarters % 2 != 0;
    for (const JPPad& p : f.pads) {
        const double x = p.x * c - p.y * s, y = p.x * s + p.y * c;
        bool matched = false;
        for (const JPPad& q : f.pads) {
            const double w = swap ? p.height : p.width, h = swap ? p.width : p.height;
            if (std::hypot(q.x - x, q.y - y) < kSameMm && std::abs(q.width - w) < kSameMm && std::abs(q.height - h) < kSameMm) {
                matched = true;
                break;
            }
        }
        if (!matched) return false;
    }
    return !f.pads.empty();
}

JPRotationCheck::Way JPRotationCheck::way(const JPFootprint& f, const std::vector<const JPPlacement*>& placements) {
    bool allUnpolarised = !placements.empty();
    bool anyPin1 = false;
    for (const JPPlacement* p : placements) {
        allUnpolarised = allUnpolarised && unpolarised(p->designator);
        anyPin1 = anyPin1 || p->hasPin1;
    }
    const bool half = symmetric(f, 2);
    if (f.pads.size() == 2 && half && allUnpolarised) return Way::CannotMatter;
    if (anyPin1 && f.pin1Pad()) return Way::File;
    if (!half && !symmetric(f, 1)) return Way::Vision;
    return Way::Person;
}

JPRotationCheck::FileVerdict JPRotationCheck::byFile(const JPFootprint& f, double turnDeg,
                                                     const std::vector<const JPPlacement*>& placements) {
    FileVerdict v;
    const JPPad* pin1 = f.pin1Pad();
    if (!pin1) return v;
    std::map<int, std::vector<std::string>> byQuarter;
    for (const JPPlacement* p : placements) {
        if (!p->hasPin1) continue;
        const double rot = (p->rotationDeg + turnDeg) * kPi / 180;
        // Pad 1 seen from the top: the footprint mirrored for the bottom side, turned, moved.
        const double lx = p->side == JPPlacement::Side::Bottom ? -pin1->x : pin1->x, ly = pin1->y;
        const double ex = lx * std::cos(rot) - ly * std::sin(rot), ey = lx * std::sin(rot) + ly * std::cos(rot);
        const double ax = p->pin1X - p->x, ay = p->pin1Y - p->y;
        if (std::hypot(ex, ey) < kSameMm || std::hypot(ax, ay) < kSameMm) continue;   // pad 1 in the middle: says nothing
        ++v.compared;
        double off = (std::atan2(ay, ax) - std::atan2(ey, ex)) * 180 / kPi;
        off = std::fmod(off + 360 * 3, 360.0);
        const int q = int(std::lround(off / 90)) % 4;
        if (std::abs(off - q * 90) > kAgreeDeg && std::abs(off - q * 90 - 360) > kAgreeDeg) {
            byQuarter[-1].push_back(p->designator);
            continue;
        }
        byQuarter[q].push_back(p->designator);
        if (q == 0) ++v.agreeing;
    }
    if (v.compared == 0) return v;
    // The quarter most placements say, and those that say otherwise.
    int best = 0;
    size_t most = 0;
    for (const auto& [q, ds] : byQuarter)
        if (q >= 0 && ds.size() > most) {
            most = ds.size();
            best = q;
        }
    v.quarters = best;
    v.uniform = most == size_t(v.compared);
    for (const auto& [q, ds] : byQuarter)
        if (q != best) v.odd.insert(v.odd.end(), ds.begin(), ds.end());
    return v;
}

bool JPRotationCheck::checked(const JPPackage& k) {
    return !k.checkedBy.empty() && k.checkedFootprintId == k.footprintId && k.checkedTurnDeg == k.turnDeg;
}

void JPRotationCheck::markChecked(JPPackage& k, const char* by) {
    k.checkedBy = by;
    k.checkedFootprintId = k.footprintId;
    k.checkedTurnDeg = k.turnDeg;
}

const char* JPRotationCheck::name(Way w) {
    switch (w) {
        case Way::CannotMatter: return "cannot matter";
        case Way::File:         return "by the file's pad 1";
        case Way::Vision:       return "by vision";
        case Way::Person:       return "by you, on the board";
    }
    return "";
}

} // inline namespace jf
