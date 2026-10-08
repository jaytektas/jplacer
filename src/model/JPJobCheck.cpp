// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPJobCheck.h"

#include "JPBoardLocation.h"
#include "JPShortages.h"

#include <algorithm>
#include <map>
#include <set>

inline namespace jf {

namespace {

// Items gathered by what they say, each naming what it is about once, in the order met.
class Gather {
public:
    void add(JPJobCheck::Item::Level level, const std::string& what, const std::string& which, const std::string& fix) {
        const auto key = std::make_pair(int(level), what);
        auto it = m_index.find(key);
        if (it == m_index.end()) {
            it = m_index.emplace(key, m_items.size()).first;
            m_items.push_back({ level, what, {}, fix });
        }
        auto& w = m_items[it->second].which;
        if (std::find(w.begin(), w.end(), which) == w.end()) w.push_back(which);
    }
    std::vector<JPJobCheck::Item> take() {
        std::stable_sort(m_items.begin(), m_items.end(), [](const auto& a, const auto& b) { return a.level < b.level; });
        return std::move(m_items);
    }

private:
    std::vector<JPJobCheck::Item>             m_items;
    std::map<std::pair<int, std::string>, size_t> m_index;
};

} // namespace

const char* JPJobCheck::levelName(Item::Level l) {
    switch (l) {
        case Item::Level::Stop:  return "Stop";
        case Item::Level::Check: return "Check";
        case Item::Level::Note:  return "Note";
    }
    return "Note";
}

bool JPJobCheck::stops(const std::vector<Item>& items) {
    return std::any_of(items.begin(), items.end(), [](const Item& i) { return i.level == Item::Level::Stop; });
}

bool JPJobCheck::asks(const std::vector<Item>& items) {
    return std::any_of(items.begin(), items.end(), [](const Item& i) { return i.level != Item::Level::Note; });
}

std::vector<JPJobCheck::Item> JPJobCheck::of(JPConfiguration& config, const JPJob& job,
                                              const std::vector<std::string>& machineTipIds) {
    using L = Item::Level;
    Gather g;
    std::set<std::string> partsSeen;
    for (const JPBoardLocation* l : job.boardLocations()) {
        if (!l || !l->isEnabled() || !l->holder) continue;
        const JPBoard* board = l->board();
        const JPBoard* def = board ? static_cast<const JPBoard*>(board->definition()) : nullptr;
        std::set<std::string> ids;
        for (const JPPlacement& p : l->holder->placements)
            if (!ids.insert(p.id).second)
                g.add(L::Stop, "A placement ID used twice on a board", l->uniqueId() + JPPlacementsHolderLocation::kIdDelimiter + p.id,
                      "Boards tab: give each placement its own ID");
        for (const JPPlacement& p : l->holder->placements) {
            if (p.type != JPPlacement::Type::Placement || !job.retrieveEnabledState(*l, &p)) continue;
            if (job.retrievePlacedStatus(*l, p.id) || p.side != l->globalSide()) continue;
            const std::string where = l->uniqueId() + JPPlacementsHolderLocation::kIdDelimiter + p.id;
            const JPBoardPart* bp = def ? def->part(p.boardPart) : nullptr;
            const JPPart* part = config.part(p.partId);
            if (!part) {
                if (bp && bp->state == JPBoardPart::State::Unmatched)
                    g.add(L::Stop, "No part chosen", where, "Boards tab: Board's Parts, or the placement's Part");
                else
                    g.add(L::Stop, "A part the library does not have", where + " (" + p.partId + ")",
                          "Boards tab: Board's Parts (choose one the library has, or make it the board's own)");
                continue;
            }
            if (p.verified.by.empty())
                g.add(L::Check, "Not verified on the machine", where, "Job tab: Verified, Next steps through them");
            if (!partsSeen.insert(part->id).second) continue;
            // The part's own checks, once.
            const JPPackage* package = config.package(part->packageId);
            if (!package) {
                g.add(L::Stop, "No package", part->id, "Parts tab: the part's Package");
                continue;
            }
            const bool tip = std::any_of(machineTipIds.begin(), machineTipIds.end(), [package](const std::string& t) {
                return std::find(package->compatibleNozzleTipIds.begin(), package->compatibleNozzleTipIds.end(), t)
                       != package->compatibleNozzleTipIds.end();
            });
            if (!tip)
                g.add(L::Stop, "No nozzle tip on the machine fits its package", part->id + " (" + package->id + ")",
                      "Packages tab: the package's Nozzle Tips");
            if (!config.findFeeder(part->id, std::nullopt))
                g.add(L::Stop, "No enabled feeder holds it", part->id, "Feeders tab: a feeder with the part, enabled");
            if (part->isPartHeightUnknown())
                g.add(L::Check, "Height not known", part->id, "Parts tab: the part's Height (or probe it)");
            const bool footprint = (bp && bp->copyFootprint && !bp->copyFootprint->geometry.pads.empty())
                                   || !package->footprint.pads.empty();
            if (!footprint)
                g.add(L::Check, "No footprint to draw or check against", part->id + " (" + package->id + ")",
                      "Packages tab: the package's Footprint, or a library footprint");
        }
    }
    for (const JPShortages::Line& s : JPShortages::of(config, job))
        // Only for parts whose stock is kept (a lot, open or closed): no lots at all is not keeping stock.
        if (s.shortBy > 0 && !config.stock().lots(s.partUuid, true).empty())
            g.add(L::Note, "Short of stock (a run asks for it as it goes)",
                  s.partId + " (short " + std::to_string(s.shortBy) + ")", "Job > Shortages…; the Parts tab's Stock page");
    return g.take();
}

} // inline namespace jf
