// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPPanel.h"
#include "JPPlacementsHolderLocation.h"

#include <memory>
#include <vector>

inline namespace jf {

// A panel on another panel or in a job, as OpenPnP's PanelLocation
// ("Pnl1"); a job's root is one too, holding the job's boards and panels.
class JPPanelLocation : public JPPlacementsHolderLocation {
public:
    static constexpr const char* kIdPrefix = "Pnl";

    JPPanelLocation() = default;
    Kind kind() const override { return Kind::Panel; }
    std::unique_ptr<JPPlacementsHolderLocation> instance() const override;

    JPPanel*       panel() { return static_cast<JPPanel*>(holder.get()); }
    const JPPanel* panel() const { return static_cast<const JPPanel*>(holder.get()); }

    // Its panel's children; none without a panel.
    std::vector<JPPlacementsHolderLocation*> children() const;
    JPPlacementsHolderLocation* addChild(std::unique_ptr<JPPlacementsHolderLocation> child);
    void removeChild(const JPPlacementsHolderLocation* child);
    // A new transform here drops those of the children (they follow it).
    void setLocalToParentTransform(std::optional<JPAffineTransform> t) override;
    // Each child's (and theirs') parent set to where it now is.
    void setParentsOfAllDescendants();

private:
    JPPanelLocation(const JPPanelLocation& o);
};

} // inline namespace jf
