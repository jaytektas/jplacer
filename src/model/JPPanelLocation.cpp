// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPPanelLocation.h"

inline namespace jf {

JPPanelLocation::JPPanelLocation(const JPPanelLocation& o) : JPPlacementsHolderLocation(o) {
    for (JPPlacementsHolderLocation* c : children()) c->parent = this;
}

std::unique_ptr<JPPlacementsHolderLocation> JPPanelLocation::instance() const {
    return std::unique_ptr<JPPlacementsHolderLocation>(new JPPanelLocation(*this));
}

std::vector<JPPlacementsHolderLocation*> JPPanelLocation::children() const {
    std::vector<JPPlacementsHolderLocation*> out;
    if (const JPPanel* p = panel())
        for (const auto& c : p->children) out.push_back(c.get());
    return out;
}

JPPlacementsHolderLocation* JPPanelLocation::addChild(std::unique_ptr<JPPlacementsHolderLocation> child) {
    child->parent = this;
    return panel()->addChild(std::move(child));
}

void JPPanelLocation::removeChild(const JPPlacementsHolderLocation* child) {
    if (JPPanel* p = panel()) p->removeChild(child);
}

void JPPanelLocation::setLocalToParentTransform(std::optional<JPAffineTransform> t) {
    JPPlacementsHolderLocation::setLocalToParentTransform(t);
    for (JPPlacementsHolderLocation* c : children()) c->setLocalToParentTransform(std::nullopt);
}

void JPPanelLocation::setParentsOfAllDescendants() {
    for (JPPlacementsHolderLocation* c : children()) {
        c->parent = this;
        if (c->kind() == Kind::Panel) static_cast<JPPanelLocation*>(c)->setParentsOfAllDescendants();
    }
}

} // inline namespace jf
