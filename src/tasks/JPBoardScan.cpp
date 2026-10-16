// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPBoardScan.h"

#include <algorithm>
#include <cmath>

inline namespace jf {

std::vector<std::vector<std::array<double, 2>>> JPBoardScan::padsOf(const JPFootprint& footprint, double degrees, bool mirrored) {
    std::vector<std::vector<std::array<double, 2>>> out;
    const JPFootprint f = footprint.inUnits(JPLengthUnit::Millimeters);
    const double t = degrees * M_PI / 180, c = std::cos(t), s = std::sin(t);
    for (const JPFootprint::Pad& fp : f.pads) {
        // The pad itself: not pad 1's mark drawn on it (not copper).
        const std::vector<JPFootprint::Outline> outlines = JPFootprint::padOutlines(fp);
        if (outlines.empty()) continue;
        std::vector<std::array<double, 2>> pad;
        for (const JPFootprint::Point& p : outlines.front()) {
            const double x = mirrored ? -p.x : p.x;
            pad.push_back({ x * c - p.y * s, x * s + p.y * c });
        }
        if (!pad.empty()) out.push_back(std::move(pad));
    }
    return out;
}

double JPBoardScan::sizeOf(const std::vector<std::vector<std::array<double, 2>>>& pads) {
    double lowX = INFINITY, highX = -INFINITY, lowY = INFINITY, highY = -INFINITY;
    for (const auto& pad : pads)
        for (const auto& p : pad) {
            lowX = std::min(lowX, p[0]);
            highX = std::max(highX, p[0]);
            lowY = std::min(lowY, p[1]);
            highY = std::max(highY, p[1]);
        }
    return pads.empty() ? 0 : std::max(highX - lowX, highY - lowY);
}

std::vector<JPBoardScan::Point> JPBoardScan::points(const JPConfiguration& config, const JPJob& job,
                                                    const JPPlacementsHolderLocation& location, std::string& why) {
    std::vector<Point> out;
    if (location.kind() != JPPlacementsHolderLocation::Kind::Board || !location.holder) {
        why = "choose a board (not a panel) on the Job tab";
        return out;
    }
    for (const JPPlacement& p : location.holder->placements) {
        if (p.side != location.globalSide()) continue;
        // A placement's pads are bare until it is placed; a fiducial's always are.
        if (p.type == JPPlacement::Type::Placement && job.retrievePlacedStatus(location, p.id)) continue;
        const JPPart* part = config.part(p.partId);
        const JPPackage* package = part ? config.package(part->packageId) : nullptr;
        if (!package) continue;
        const JPLocation design = p.location.convertToUnits(JPLengthUnit::Millimeters);
        // A board bottom side up is seen mirrored: its design's X the other way, so a turn and a shift fit it.
        const bool bottom = location.globalSide() == JPSide::Bottom;
        const double designX = bottom ? -design.x() : design.x();
        const JPLocation at = location.placementLocation(p.location).convertToUnits(JPLengthUnit::Millimeters);
        auto pads = padsOf(package->footprint, at.rotation(), bottom);
        const double size = sizeOf(pads);
        if (pads.empty() || !(size > 0) || size > kMostSizeMm) continue;
        out.push_back({ p.id, designX, design.y(), at.x(), at.y(), size, std::move(pads) });
    }
    if (out.empty()) why = "the board has no bare footprint with pads, up to " + std::to_string(int(kMostSizeMm)) + " mm across";
    return out;
}

} // inline namespace jf
