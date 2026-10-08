// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPJobPlan.h"

#include "JPBoardLocation.h"

#include <algorithm>
#include <iterator>

inline namespace jf {

std::string JPJobPlan::sortOf(const JPJob& job) {
    for (const char* s : kSorts)
        if (job.planSort == s) return s;
    return kMachine;
}

std::vector<JPJobPlan::Group> JPJobPlan::groups(JPConfiguration& config, const JPJob& job) {
    std::vector<Group> out;
    std::map<std::string, size_t> at;
    for (const JPBoardLocation* l : job.boardLocations()) {
        if (!l || !l->isEnabled() || !l->holder) continue;
        for (const JPPlacement& p : l->holder->placements) {
            if (p.type != JPPlacement::Type::Placement || !job.retrieveEnabledState(*l, &p)) continue;
            if (job.retrievePlacedStatus(*l, p.id) || p.side != l->globalSide()) continue;
            auto it = at.find(p.partId);
            if (it == at.end()) {
                Group g;
                g.partId = p.partId;
                if (const JPPart* part = config.part(p.partId)) {
                    g.packageId = part->packageId;
                    g.heightMm = part->height.convertToUnits(JPLengthUnit::Millimeters).value();
                    if (const JPPackage* k = config.package(part->packageId)) {
                        const JPFootprint mm = k->footprint.inMillimeters();
                        g.bodyMm2 = mm.bodyWidth * mm.bodyHeight;
                    }
                }
                if (const JPFeeder* f = config.findFeeder(p.partId, std::nullopt)) g.feederName = f->name();
                it = at.emplace(p.partId, out.size()).first;
                out.push_back(g);
            }
            ++out[it->second].left;
        }
    }
    const std::string sort = sortOf(job);
    // Unknown (0) sorts as large: a part of no known height or size is not put first.
    auto known = [](double v) { return v > 0 ? v : 1e18; };
    std::stable_sort(out.begin(), out.end(), [&](const Group& a, const Group& b) {
        if (sort == kHeight && known(a.heightMm) != known(b.heightMm)) return known(a.heightMm) < known(b.heightMm);
        if ((sort == kHeight || sort == kPackage) && known(a.bodyMm2) != known(b.bodyMm2)) return known(a.bodyMm2) < known(b.bodyMm2);
        if (sort == kPackage && known(a.heightMm) != known(b.heightMm)) return known(a.heightMm) < known(b.heightMm);
        if (sort == kMost && a.left != b.left) return a.left > b.left;
        return a.partId < b.partId;
    });
    // Moved by hand: those parts first, in their order; the rest after, sorted.
    std::stable_partition(out.begin(), out.end(), [&job](const Group& g) {
        return std::find(job.planOrder.begin(), job.planOrder.end(), g.partId) != job.planOrder.end();
    });
    const auto handEnd = std::find_if(out.begin(), out.end(), [&job](const Group& g) {
        return std::find(job.planOrder.begin(), job.planOrder.end(), g.partId) == job.planOrder.end();
    });
    std::stable_sort(out.begin(), handEnd, [&job](const Group& a, const Group& b) {
        return std::find(job.planOrder.begin(), job.planOrder.end(), a.partId) <
               std::find(job.planOrder.begin(), job.planOrder.end(), b.partId);
    });
    return out;
}

std::map<std::string, size_t> JPJobPlan::order(JPConfiguration& config, const JPJob& job) {
    std::map<std::string, size_t> out;
    if (sortOf(job) == kMachine && job.planOrder.empty()) return out;
    const auto g = groups(config, job);
    for (size_t i = 0; i < g.size(); ++i) out[g[i].partId] = i;
    return out;
}

void JPJobPlan::move(JPJob& job, const std::vector<Group>& shown, size_t from, size_t to) {
    if (from >= shown.size() || to >= shown.size() || from == to) return;
    std::vector<std::string> order;
    for (const Group& g : shown) order.push_back(g.partId);
    const std::string moved = order[from];
    order.erase(order.begin() + long(from));
    order.insert(order.begin() + long(to), moved);
    // What was moved by hand before (parts done since) is kept after, so its place is not lost.
    for (const std::string& id : job.planOrder)
        if (std::find(order.begin(), order.end(), id) == order.end()) order.push_back(id);
    job.planOrder = std::move(order);
}

} // inline namespace jf
