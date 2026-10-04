// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPLength.h"
#include "JPPlacementsHolder.h"
#include "JPPlacementsHolderLocation.h"

#include "openpnp/JPXmlElement.h"
#include "openpnp/JPXmlNode.h"

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

inline namespace jf {

class JPBoardLocation;
class JPPanelLocation;

// A panel, as OpenPnP's Panel (a .panel.xml file, or a job's root): its
// children (boards and panels, each where it lies), its own placements
// (fiducials and the like), and pseudo-placements: placements of a child
// board used as the panel's own, by unique id ("Brd1⇒FID1").
class JPPanel : public JPPlacementsHolder {
public:
    // What an older job's panel (columns and rows of one board) said, for
    // converting it.
    struct Legacy {
        std::string              id;
        int                      columns = 1, rows = 1;
        JPLength                 xGap { 0, JPLengthUnit::Millimeters };
        JPLength                 yGap { 0, JPLengthUnit::Millimeters };
        std::string              partId;
        bool                     checkFids = false;
        std::vector<JPPlacement> fiducials;
    };

    JPPanel() = default;
    JPPanel(const JPPanel& o);
    JPPanel& operator=(const JPPanel&) = delete;

    Kind kind() const override { return Kind::Panel; }
    std::shared_ptr<JPPlacementsHolder> instance() const override;

    std::vector<std::unique_ptr<JPPlacementsHolderLocation>> children;
    std::vector<std::string>                                 pseudoPlacementIds;
    std::optional<Legacy>                                    legacy;

    // Adds a child, giving it a new id ("Brd1", "Pnl1") when it has none or
    // one already taken. The child it now is.
    JPPlacementsHolderLocation* addChild(std::unique_ptr<JPPlacementsHolderLocation> child);
    // Takes a child away, with the pseudo-placements that came from it.
    void removeChild(const JPPlacementsHolderLocation* child);
    JPPlacementsHolderLocation* child(const std::string& id);

    // Every board and panel under this one, depth first.
    std::vector<JPPlacementsHolderLocation*> descendants() const;
    std::vector<JPBoardLocation*>            descendantBoardLocations() const;
    std::vector<JPPanelLocation*>            descendantPanelLocations() const;
    // The placement a unique id ("Brd1⇒R1", "Pnl1⇒Brd2⇒FID1") names, and the
    // boards and panels on the way to it; empty when there is none.
    std::pair<std::vector<JPPlacementsHolderLocation*>, const JPPlacement*>
        descendantPlacement(const std::string& uniqueId) const;
    // How many times `holder` (its definition) is used under this panel.
    int  instanceCount(const JPPlacementsHolder& holder) const;
    bool isDefinitionUsed(const JPPlacementsHolder& holder) const;

    // The pseudo-placements, worked out from where their placements are now
    // (ids that name nothing are left out).
    std::vector<JPPlacement> pseudoPlacements() const;
    // A pseudo-placement's location on this panel; false when the id names nothing.
    bool pseudoPlacementLocation(const std::string& id, JPPlacement& out) const;

    static JPPanel fromXml(const JPXmlElement& root);
    // As a .panel.xml file ("openpnp-panel"), or a job's "root-panel".
    JPXmlNode toXml(const char* elementName = "openpnp-panel") const;
};

} // inline namespace jf
