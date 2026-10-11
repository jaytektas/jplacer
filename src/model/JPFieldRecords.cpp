// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPFieldRecords.h"

#include <set>

inline namespace jf {

namespace {

using O = JPFieldSet::Of;

// A holder and its uses: `f` given each; the definition marked dirty when any changed.
bool holderAndUses(JPConfiguration& config, JPJob* job, JPPlacementsHolder& def,
                   const std::function<bool(JPPlacementsHolder&)>& f) {
    bool changed = f(def);
    for (JPPlacementsHolder* h : config.instancesOf(def, job)) changed = f(*h) || changed;
    if (changed) def.dirty = true;
    return changed;
}

// Every location under `children`, and under each panel's there (a use's own copies), given to `f`.
bool locations(std::vector<std::unique_ptr<JPPlacementsHolderLocation>>& children, const JPFieldRecords::Visit& f) {
    bool changed = false;
    for (auto& c : children) {
        changed = f(c->custom) || changed;
        if (c->kind() == JPPlacementsHolderLocation::Kind::Panel)
            if (JPPanel* p = static_cast<JPPanelLocation&>(*c).panel()) changed = locations(p->children, f) || changed;
    }
    return changed;
}

} // namespace

bool JPFieldRecords::each(JPConfiguration& config, JPJob* job, JPFieldSet::Of of, const Visit& f) {
    bool changed = false;
    switch (of) {
        case O::Parts:
            for (const auto& p : config.parts()) changed = f(p->custom) || changed;
            break;
        case O::Packages:
            for (const auto& k : config.packages()) changed = f(k->custom) || changed;
            break;
        case O::Pads:
            for (const auto& k : config.packages())
                for (JPFootprint::Pad& p : k->footprint.pads) changed = f(p.custom) || changed;
            break;
        case O::Placements: {
            auto placements = [&f](JPPlacementsHolder& h) {
                bool any = false;
                for (JPPlacement& p : h.placements) any = f(p.custom) || any;
                return any;
            };
            for (const auto& b : config.boards()) changed = holderAndUses(config, job, *b, placements) || changed;
            for (const auto& p : config.panels()) changed = holderAndUses(config, job, *p, placements) || changed;
            break;
        }
        case O::Boards:
        case O::Panels: {
            auto own = [&f](JPPlacementsHolder& h) { return f(h.custom); };
            if (of == O::Boards)
                for (const auto& b : config.boards()) changed = holderAndUses(config, job, *b, own) || changed;
            else
                for (const auto& p : config.panels()) changed = holderAndUses(config, job, *p, own) || changed;
            break;
        }
        case O::Locations:
            for (const auto& p : config.panels())
                if (locations(p->children, f)) {
                    p->dirty = true;
                    changed = true;
                }
            if (job && job->root().panel() && locations(job->root().panel()->children, f)) {
                job->dirty = true;
                changed = true;
            }
            break;
        case O::Feeders:
            for (JPFeeder& fd : config.feeders()) changed = f(fd.custom) || changed;
            break;
        case O::Vision:
            for (JPVisionSettings& v : config.visionSettings()) changed = f(v.custom) || changed;
            break;
        case O::Job:
            if (job && f(job->custom)) {
                job->dirty = true;
                changed = true;
            }
            break;
    }
    return changed;
}

int JPFieldRecords::count(JPConfiguration& config, JPJob* job, JPFieldSet::Of of, const std::string& key) {
    int n = 0;
    if (of == O::Placements || of == O::Boards || of == O::Panels) {
        // Once a definition: its uses hold copies of its values.
        auto own = [&](const JPPlacementsHolder& h) {
            if (of != O::Placements) n += h.custom.has(key);
            else
                for (const JPPlacement& p : h.placements) n += p.custom.has(key);
        };
        if (of != O::Panels)
            for (const auto& b : config.boards()) own(*b);
        if (of != O::Boards)
            for (const auto& p : config.panels()) own(*p);
        return n;
    }
    each(config, job, of, [&](JPFieldValues& v) {
        n += v.has(key);
        return false;
    });
    return n;
}

int JPFieldRecords::erase(JPConfiguration& config, JPJob* job, JPFieldSet::Of of, const std::string& key) {
    const int n = count(config, job, of, key);
    each(config, job, of, [&key](JPFieldValues& v) {
        if (!v.has(key)) return false;
        v.erase(key);
        return true;
    });
    return n;
}

std::vector<std::string> JPFieldRecords::unconvertible(JPConfiguration& config, JPJob* job, JPFieldSet::Of of,
                                                       const JPField& from, const JPField& to) {
    std::vector<std::string> out;
    std::set<std::string> said;
    each(config, job, of, [&](JPFieldValues& v) {
        const JJson& x = v.get(from.key);
        if (x.isNull()) return false;
        JJson y;
        std::string error;
        const std::string t = from.text(x);
        if (!to.parse(t, y, error) && said.insert(t).second) out.push_back(t);
        return false;
    });
    return out;
}

void JPFieldRecords::convert(JPConfiguration& config, JPJob* job, JPFieldSet::Of of, const JPField& from, const JPField& to) {
    each(config, job, of, [&](JPFieldValues& v) {
        const JJson& x = v.get(from.key);
        if (x.isNull()) return false;
        JJson y;
        std::string error;
        if (!to.parse(from.text(x), y, error)) y = JJson();
        if (y.dump() == x.dump()) return false;
        v.set(to.key, y);
        return true;
    });
}

} // inline namespace jf
