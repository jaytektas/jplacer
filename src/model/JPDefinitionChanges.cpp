// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPDefinitionChanges.h"

#include <algorithm>

inline namespace jf {

void JPDefinitionChanges::added(JPPlacementsHolder& def, const JPPlacement& p) {
    def.placements.push_back(p);
    for (JPPlacementsHolder* h : m_config.instancesOf(def, m_job)) h->placements.push_back(p);
    def.dirty = true;
}

void JPDefinitionChanges::removed(JPPlacementsHolder& def, const std::string& id) {
    auto drop = [&id](JPPlacementsHolder& h) {
        std::erase_if(h.placements, [&id](const JPPlacement& p) { return p.id == id; });
    };
    // The instances first: they are found through the definition.
    for (JPPlacementsHolder* h : m_config.instancesOf(def, m_job)) drop(*h);
    drop(def);
    def.dirty = true;
}

} // inline namespace jf
