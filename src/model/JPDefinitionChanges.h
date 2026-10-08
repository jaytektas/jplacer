// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPConfiguration.h"
#include "JPJob.h"
#include "JPPanel.h"

#include <string>

inline namespace jf {

// A change to a board's or panel's definition, carried to every instance of
// it (in the job, on the known panels), as OpenPnP's Definable carries
// each property set on a definition to the objects it defines; what an
// instance sets for itself is kept unless the same property is set on the
// definition. The definition is marked changed (dirty).
class JPDefinitionChanges {
public:
    JPDefinitionChanges(JPConfiguration& config, const JPJob* job) : m_config(config), m_job(job) {}

    // `set` applied to the definition's placement `id` and the same
    // placement on each instance.
    template <class F>
    void placement(JPPlacementsHolder& def, const std::string& id, F set) {
        JPPlacement* p = def.find(id);
        if (!p) return;
        set(*p);
        for (JPPlacementsHolder* h : m_config.instancesOf(def, m_job))
            if (JPPlacement* q = h->find(id)) set(*q);
        def.dirty = true;
    }
    // `set` applied to the definition itself (its name, dimensions…) and each instance.
    template <class F>
    void holder(JPPlacementsHolder& def, F set) {
        set(def);
        for (JPPlacementsHolder* h : m_config.instancesOf(def, m_job)) set(*h);
        def.dirty = true;
    }
    // A placement added to the definition, and a copy of it to each instance.
    void added(JPPlacementsHolder& def, const JPPlacement& p);
    // The board's revision shown changed (made, switched or work carried undone): each instance shows it
    // (JPBoard::followRevision).
    void revisionShown(JPBoard& def);
    // A placement taken from the definition and from each instance.
    void removed(JPPlacementsHolder& def, const std::string& id);

    // `set` applied to a panel's child `id` (where it lies, its side…) and
    // the same child of each use of the panel.
    template <class F>
    void child(JPPanel& def, const std::string& id, F set) {
        JPPlacementsHolderLocation* c = def.child(id);
        if (!c) return;
        set(*c);
        for (JPPlacementsHolder* h : m_config.instancesOf(def, m_job))
            if (JPPlacementsHolderLocation* d = static_cast<JPPanel*>(h)->child(id)) set(*d);
        def.dirty = true;
    }
    // A child added to the panel (an id given it), and a use of it to each
    // use of the panel. The child it now is.
    JPPlacementsHolderLocation* childAdded(JPPanel& def, std::unique_ptr<JPPlacementsHolderLocation> c);
    // A child taken away, from the panel and each use of it.
    void childRemoved(JPPanel& def, const std::string& id);
    // A child replaced by another board or panel, in the panel and each use of it.
    void childReplaced(JPPanel& def, const std::string& id, const JPPlacementsHolderLocation& replacement);
    // The panel's pseudo-placements (its alignment ones) given to each use of it.
    void pseudoPlacementsChanged(JPPanel& def);

private:
    JPConfiguration& m_config;
    const JPJob*     m_job;
};

} // inline namespace jf
