// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPBoardScan.h"

#include <algorithm>
#include <cmath>

inline namespace jf {

std::optional<double> JPBoardScan::mirroredSize(const JPFootprint& footprint) {
    const JPFootprint f = footprint.inUnits(JPLengthUnit::Millimeters);
    if (f.pads.empty()) return std::nullopt;
    auto alike = [](const JPFootprint::Pad& a, const JPFootprint::Pad& b) {
        return std::abs(a.width - b.width) <= kSameMm && std::abs(a.height - b.height) <= kSameMm;
    };
    // Each pad has its mirror through the middle (a pad in the middle, itself).
    for (const JPFootprint::Pad& p : f.pads) {
        const bool mirrored = std::any_of(f.pads.begin(), f.pads.end(), [&](const JPFootprint::Pad& q) {
            return std::abs(q.x + p.x) <= kSameMm && std::abs(q.y + p.y) <= kSameMm && alike(p, q);
        });
        if (!mirrored) return std::nullopt;
    }
    double lowX = INFINITY, highX = -INFINITY, lowY = INFINITY, highY = -INFINITY;
    for (const JPFootprint::Pad& p : f.pads) {
        // A turned pad's box: its width and height swapped at 90, as wide as its diagonal otherwise.
        const double t = p.rotation * M_PI / 180;
        const double hw = 0.5 * (std::abs(p.width * std::cos(t)) + std::abs(p.height * std::sin(t)));
        const double hh = 0.5 * (std::abs(p.width * std::sin(t)) + std::abs(p.height * std::cos(t)));
        lowX = std::min(lowX, p.x - hw);
        highX = std::max(highX, p.x + hw);
        lowY = std::min(lowY, p.y - hh);
        highY = std::max(highY, p.y + hh);
    }
    const double size = std::max(highX - lowX, highY - lowY);
    if (!(size > 0)) return std::nullopt;
    return size;
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
        const auto size = mirroredSize(package->footprint);
        if (!size) continue;
        const JPLocation design = p.location.convertToUnits(JPLengthUnit::Millimeters);
        // A board bottom side up is seen mirrored: its design's X the other way, so a turn and a shift fit it.
        const double designX = location.globalSide() == JPSide::Bottom ? -design.x() : design.x();
        const JPLocation at = location.placementLocation(p.location).convertToUnits(JPLengthUnit::Millimeters);
        const double turn = std::remainder(at.rotation(), 90.0);
        if (std::abs(turn) > kSquareDeg) continue;
        out.push_back({ p.id, designX, design.y(), at.x(), at.y(), *size });
    }
    if (out.empty()) why = "the board has no bare footprint whose pads mirror through its middle, square to the machine";
    return out;
}

} // inline namespace jf
