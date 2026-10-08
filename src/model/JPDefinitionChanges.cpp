// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPDefinitionChanges.h"

#include "JPPanelLocation.h"

#include <algorithm>

inline namespace jf {

void JPDefinitionChanges::added(JPPlacementsHolder& def, const JPPlacement& p) {
    def.placements.push_back(p);
    for (JPPlacementsHolder* h : m_config.instancesOf(def, m_job)) h->placements.push_back(p);
    def.dirty = true;
}

void JPDefinitionChanges::revisionShown(JPBoard& def) {
    for (JPPlacementsHolder* h : m_config.instancesOf(def, m_job))
        if (h->kind() == JPPlacementsHolder::Kind::Board) static_cast<JPBoard*>(h)->followRevision(def);
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

JPPlacementsHolderLocation* JPDefinitionChanges::childAdded(JPPanel& def, std::unique_ptr<JPPlacementsHolderLocation> c) {
    JPPlacementsHolderLocation* added = def.addChild(std::move(c));
    for (JPPlacementsHolderLocation* l : m_config.instanceLocationsOf(def, m_job)) {
        auto* panel = static_cast<JPPanelLocation*>(l);
        JPPlacementsHolderLocation* use = panel->panel()->addChild(added->instance());
        use->id = added->id;
        panel->setParentsOfAllDescendants();
    }
    def.dirty = true;
    return added;
}

void JPDefinitionChanges::childRemoved(JPPanel& def, const std::string& id) {
    for (JPPlacementsHolder* h : m_config.instancesOf(def, m_job)) {
        auto* panel = static_cast<JPPanel*>(h);
        panel->removeChild(panel->child(id));
    }
    def.removeChild(def.child(id));
    def.dirty = true;
}

void JPDefinitionChanges::childReplaced(JPPanel& def, const std::string& id, const JPPlacementsHolderLocation& replacement) {
    const JPPlacementsHolderLocation* old = def.child(id);
    if (!old) return;
    // The panel's own child a definition of its own; each use's an instance of it.
    auto own = replacement.instance();
    own->makeDefinition();
    const JPPlacementsHolderLocation* fresh = def.replaceChild(old, std::move(own));
    for (JPPlacementsHolderLocation* l : m_config.instanceLocationsOf(def, m_job)) {
        auto* panel = static_cast<JPPanelLocation*>(l);
        if (const JPPlacementsHolderLocation* was = panel->panel()->child(id)) {
            panel->panel()->replaceChild(was, fresh->instance());
            panel->setParentsOfAllDescendants();
        }
    }
    def.dirty = true;
}

void JPDefinitionChanges::pseudoPlacementsChanged(JPPanel& def) {
    for (JPPlacementsHolder* h : m_config.instancesOf(def, m_job))
        static_cast<JPPanel*>(h)->pseudoPlacementIds = def.pseudoPlacementIds;
    def.dirty = true;
}

} // inline namespace jf
