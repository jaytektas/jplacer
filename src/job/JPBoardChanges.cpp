// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPBoardChanges.h"

#include <cmath>
#include <cstdio>
#include <map>

inline namespace jf {

namespace {

constexpr double kMovedMm = 0.001, kTurnedDeg = 0.01;

std::string mm(double v) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.2f", v);
    return buf;
}

std::string identity(const JPPlacement& p) {
    std::string s = p.value + "|" + p.mpn + "|" + p.footprint;
    for (const JPSupplierNumber& n : p.supplierNumbers) s += "|" + n.supplier + ":" + n.number;
    return s;
}

std::string partText(const JPPlacement& p) {
    std::string s = p.value;
    if (!p.mpn.empty() && p.mpn != p.value) s += (s.empty() ? "" : " ") + p.mpn;
    return s.empty() ? p.footprint : s;
}

double turned(const JPPlacement& a, const JPPlacement& b) {
    double d = std::fmod(std::abs(a.rotationDeg - b.rotationDeg), 360.0);
    return std::min(d, 360.0 - d);
}

} // namespace

const char* JPBoardChanges::mark(Change::Kind k) {
    switch (k) {
        case Change::Kind::Added:   return "+";
        case Change::Kind::Removed: return "\xE2\x88\x92";
        default:                    return "~";
    }
}

std::vector<JPBoardChanges::Change> JPBoardChanges::compare(const JPBoard& before, const JPBoard& after) {
    std::vector<Change> out;
    std::map<std::string, const JPPlacement*> old;
    for (const JPPlacement& p : before.placements) old[p.designator] = &p;
    for (const JPPlacement& p : after.placements) {
        const auto it = old.find(p.designator);
        if (it == old.end()) {
            out.push_back({ Change::Kind::Added, p.designator, partText(p) + " new" });
            continue;
        }
        const JPPlacement& o = *it->second;
        if (identity(o) != identity(p))
            out.push_back({ Change::Kind::DifferentPart, p.designator, partText(o) + " \xE2\x86\x92 " + partText(p) });
        const double moved = std::hypot(p.x - o.x, p.y - o.y);
        if (moved > kMovedMm || p.side != o.side)
            out.push_back({ Change::Kind::Moved, p.designator,
                            p.side != o.side ? std::string("moved to the other side") : "moved " + mm(moved) + " mm" });
        if (turned(o, p) > kTurnedDeg)
            out.push_back({ Change::Kind::Turned, p.designator, "turned " + mm(turned(o, p)) + "\xC2\xB0" });
        old.erase(it);
    }
    for (const auto& [d, p] : old) out.push_back({ Change::Kind::Removed, d, partText(*p) + " removed" });
    return out;
}

JPBoard JPBoardChanges::merge(const JPBoard& before, JPBoard after) {
    std::map<std::string, const JPPlacement*> old;
    for (const JPPlacement& p : before.placements) old[p.designator] = &p;
    for (JPPlacement& p : after.placements) {
        const auto it = old.find(p.designator);
        if (it == old.end()) continue;
        const JPPlacement& o = *it->second;
        p.rotationSet = o.rotationSet;
        p.rotationSetDeg = o.rotationSetDeg;
        p.reference = o.reference;
        const bool moved = std::hypot(p.x - o.x, p.y - o.y) > kMovedMm || p.side != o.side;
        if (!moved) {
            p.recorded = o.recorded;
            p.recordedX = o.recordedX;
            p.recordedY = o.recordedY;
            p.lookWidth = o.lookWidth;
            p.lookPxPerMm = o.lookPxPerMm;
            p.look = o.look;
        }
        if (identity(o) == identity(p)) {
            p.partId = o.partId;
            p.partGuessed = o.partGuessed;
        }
    }
    return after;
}

} // inline namespace jf
