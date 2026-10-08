// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPShortages.h"

#include "JPBoardLocation.h"

#include <algorithm>
#include <cmath>
#include <map>

inline namespace jf {

std::vector<JPShortages::Line> JPShortages::of(const JPConfiguration& config, const JPJob& job) {
    std::map<std::string, Line> byPart;
    for (const JPBoardLocation* l : job.boardLocations()) {
        if (!l || !l->holder || !l->isEnabled()) continue;
        for (const JPPlacement& p : l->holder->placements)
            if (p.side == l->globalSide() && p.type == JPPlacement::Type::Placement && p.enabled
                && !job.retrievePlacedStatus(*l, p.id)) {
                Line& line = byPart[p.partId];
                line.partId = p.partId;
                ++line.needed;
            }
    }
    const JPStockStore& stock = config.stock();
    const std::map<std::string, long long> onHand = stock.onHandByPart();
    std::vector<Line> out;
    for (auto& [id, line] : byPart) {
        if (const JPPart* part = config.libraryPart(id); part && !part->uuid.empty()) {
            line.partUuid = part->uuid;
            if (const auto set = stock.attritionSet(part->uuid)) {
                line.rate = *set;
                line.rateFrom = Line::Rate::Set;
            } else if (const JPStockStore::Attrition a = stock.attrition(part->uuid); a.used > 0) {
                line.rate = a.rate();
                line.rateFrom = Line::Rate::Measured;
            }
            line.attrition = int(std::ceil(line.needed * line.rate - 1e-9));
            if (const auto h = onHand.find(part->uuid); h != onHand.end()) line.inStock = h->second;
            line.lots = stock.lots(part->uuid, false);
            line.shortBy = std::max(0LL, line.needed + line.attrition - line.inStock);
        }
        out.push_back(line);
    }
    std::sort(out.begin(), out.end(), [](const Line& a, const Line& b) {
        if (a.shortBy != b.shortBy) return a.shortBy > b.shortBy;
        return a.partId < b.partId;
    });
    return out;
}

} // inline namespace jf
