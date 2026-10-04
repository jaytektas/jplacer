// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPAffineTransform.h"
#include "JPLocation.h"
#include "JPPlacementsHolder.h"
#include "JPSide.h"

#include "openpnp/JPXmlElement.h"
#include "openpnp/JPXmlNode.h"

#include <memory>
#include <optional>
#include <string>

inline namespace jf {

class JPPanelLocation;

// Where a board or panel lies in its parent (a panel, or the job), as
// OpenPnP's PlacementsHolderLocation: its id, side, location, the file it
// comes from, whether its fiducials are checked, whether it is enabled,
// and the transform that takes its placements to its parent's coordinates
// (worked out from the location, or set by a fiducial check).
class JPPlacementsHolderLocation {
public:
    enum class Kind { Board, Panel };
    enum class TransformStatus { NotSet, GloballySet, LocallySet };
    // Between the ids of a unique id: "Pnl1⇒Brd2⇒R1".
    static constexpr const char* kIdDelimiter = "\xE2\x87\x92";

    virtual ~JPPlacementsHolderLocation() = default;
    virtual Kind kind() const = 0;
    // An instance of this one, with an instance of its board or panel; its
    // definition() is this one's.
    virtual std::unique_ptr<JPPlacementsHolderLocation> instance() const = 0;

    std::string id;
    JPSide      side = JPSide::Top;
    std::string fileName;
    bool        checkFiducials = false;
    bool        locallyEnabled = true;
    JPPanelLocation*                    parent = nullptr;
    std::shared_ptr<JPPlacementsHolder> holder;   // its board or panel (an instance)

    const JPLocation& location() const { return m_location; }
    // A new location drops a transform set by a fiducial check.
    void setLocation(const JPLocation& l);

    const JPPlacementsHolderLocation* definition() const { return m_definition ? m_definition : this; }
    // A definition of its own from now on (a copy made one, as a panel's
    // new child is).
    void makeDefinition() { m_definition = nullptr; }

    // The side as seen from the machine: flipped under a panel bottom side up.
    JPSide globalSide() const;
    void   setGlobalSide(JPSide s);
    void   flipSide();
    bool   isParentBranchEnabled() const;
    bool   isEnabled() const { return locallyEnabled && isParentBranchEnabled(); }
    // Its parents' ids and its own: "Pnl1⇒Brd2".
    std::string uniqueId() const;
    bool isDescendantOf(const JPPlacementsHolderLocation& potentialAncestor) const;

    JPAffineTransform localToParentTransform() const;
    virtual void setLocalToParentTransform(std::optional<JPAffineTransform> t);
    JPAffineTransform localToGlobalTransform() const;
    void setLocalToGlobalTransform(const JPAffineTransform& t);
    TransformStatus transformStatus() const;
    void setTransformStatus(TransformStatus s) { m_status = s; }

    JPLocation globalLocation() const;
    void setGlobalLocation(const JPLocation& l);

    // Where a placement at `local` (on this board or panel) is in the job's
    // (machine) coordinates, as OpenPnP's Utils2D.calculateBoardPlacementLocation;
    // `localOnly`: in this one's parent's instead.
    JPLocation placementLocation(const JPLocation& local, bool localOnly = false) const;
    // Where a child board or panel's origin is, and its angle.
    JPLocation childLocation(const JPPlacementsHolderLocation& child, bool localOnly = false) const;
    // The inverse: from the job's coordinates onto this board or panel.
    JPLocation placementLocationInverse(const JPLocation& global) const;
    // The transform OpenPnP works out from the location and side alone.
    JPAffineTransform defaultTransform() const;

    // The <object class="org.openpnp.model.BoardLocation" …> element.
    JPXmlNode toXml() const;
    static std::unique_ptr<JPPlacementsHolderLocation> fromXml(const JPXmlElement& e);

protected:
    JPPlacementsHolderLocation() = default;
    JPPlacementsHolderLocation(const JPPlacementsHolderLocation& o);
    JPPlacementsHolderLocation& operator=(const JPPlacementsHolderLocation&) = delete;

    JPLocation                                 m_location;
    mutable std::optional<JPAffineTransform>   m_localToParent;
    TransformStatus                            m_status = TransformStatus::NotSet;
    const JPPlacementsHolderLocation*          m_definition = nullptr;
};

} // inline namespace jf
