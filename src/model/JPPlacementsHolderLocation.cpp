// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPPlacementsHolderLocation.h"

#include "JPBoardLocation.h"
#include "JPLocationXml.h"
#include "JPPanelLocation.h"
#include "JPSides.h"
#include "JPXmlValues.h"

#include <cmath>

inline namespace jf {

using V = JPXmlValues;

namespace {
constexpr const char* kBoardLocationClass = "org.openpnp.model.BoardLocation";
constexpr const char* kPanelLocationClass = "org.openpnp.model.PanelLocation";
}

JPPlacementsHolderLocation::JPPlacementsHolderLocation(const JPPlacementsHolderLocation& o)
    : id(o.id), side(o.side), fileName(o.fileName), checkFiducials(o.checkFiducials), locallyEnabled(o.locallyEnabled),
      parent(o.parent), holder(o.holder ? o.holder->instance() : nullptr), m_location(o.m_location),
      m_localToParent(o.m_localToParent), m_status(o.m_status), m_definition(o.definition()) {}

void JPPlacementsHolderLocation::setLocation(const JPLocation& l) {
    if (!(l == m_location)) setLocalToParentTransform(std::nullopt);
    m_location = l;
}

JPSide JPPlacementsHolderLocation::globalSide() const {
    if (parent && parent->globalSide() == JPSide::Bottom) return JPSides::flip(side);
    return side;
}

void JPPlacementsHolderLocation::setGlobalSide(JPSide s) {
    side = parent && parent->globalSide() == JPSide::Bottom ? JPSides::flip(s) : s;
}

void JPPlacementsHolderLocation::flipSide() {
    setGlobalSide(JPSides::flip(globalSide()));
}

bool JPPlacementsHolderLocation::isParentBranchEnabled() const {
    return parent ? parent->isEnabled() : true;
}

std::string JPPlacementsHolderLocation::uniqueId() const {
    if (parent) {
        const std::string p = parent->uniqueId();
        if (!p.empty()) return p + kIdDelimiter + id;
    }
    return id;
}

bool JPPlacementsHolderLocation::isDescendantOf(const JPPlacementsHolderLocation& potentialAncestor) const {
    for (const JPPlacementsHolderLocation* a = parent; a && !a->fileName.empty(); a = a->parent)
        if (a->fileName == potentialAncestor.fileName) return true;
    return false;
}

JPAffineTransform JPPlacementsHolderLocation::defaultTransform() const {
    const JPLocation l = m_location.convertToUnits(JPLengthUnit::Millimeters);
    JPAffineTransform t;
    t.translate(l.x(), l.y());
    t.rotate(l.rotation() * M_PI / 180);
    if (side == JPSide::Bottom) {
        const double width = holder ? holder->dimensions.convertToUnits(JPLengthUnit::Millimeters).x() : 0;
        t.translate(width, 0);
        t.scale(-1, 1);
    }
    return t;
}

JPAffineTransform JPPlacementsHolderLocation::localToParentTransform() const {
    if (!m_localToParent && holder) m_localToParent = defaultTransform();
    return m_localToParent ? *m_localToParent : defaultTransform();
}

void JPPlacementsHolderLocation::setLocalToParentTransform(std::optional<JPAffineTransform> t) {
    m_localToParent = t;
    if (!t) m_status = TransformStatus::NotSet;
}

JPAffineTransform JPPlacementsHolderLocation::localToGlobalTransform() const {
    JPAffineTransform t = localToParentTransform();
    if (parent) t.preConcatenate(parent->localToGlobalTransform());
    return t;
}

void JPPlacementsHolderLocation::setLocalToGlobalTransform(const JPAffineTransform& global) {
    JPAffineTransform local = global;
    if (parent)
        if (const auto inv = parent->localToGlobalTransform().inverse()) local.preConcatenate(*inv);
    setLocalToParentTransform(local);
    m_status = TransformStatus::LocallySet;
}

JPPlacementsHolderLocation::TransformStatus JPPlacementsHolderLocation::transformStatus() const {
    if (m_status == TransformStatus::LocallySet) return m_status;
    if (parent && parent->transformStatus() != TransformStatus::NotSet) return TransformStatus::GloballySet;
    return TransformStatus::NotSet;
}

JPLocation JPPlacementsHolderLocation::globalLocation() const {
    return parent ? parent->childLocation(*this) : m_location;
}

void JPPlacementsHolderLocation::setGlobalLocation(const JPLocation& l) {
    setLocation(parent ? parent->placementLocationInverse(l) : l);
}

namespace {

// OpenPnP's Utils2D.calculateBoardPlacementLocation once the placement's own
// location (`p`) and the sign its angle is added with are known.
JPLocation placed(const JPPlacementsHolderLocation& bl, JPLocation p, double angleSign, bool localOnly) {
    const JPAffineTransform t = localOnly ? bl.localToParentTransform() : bl.localToGlobalTransform();
    const JPLocation board = (localOnly ? bl.location() : bl.globalLocation()).convertToUnits(JPLengthUnit::Millimeters);
    const JPLengthUnit units = p.units();
    p = p.convertToUnits(JPLengthUnit::Millimeters);
    const double angle = t.info().rotationAngleDeg;
    double x, y;
    t.apply(p.x(), p.y(), x, y);
    return JPLocation(JPLengthUnit::Millimeters, x, y, board.z() + p.z(), angle + angleSign * p.rotation()).convertToUnits(units);
}

} // namespace

JPLocation JPPlacementsHolderLocation::placementLocation(const JPLocation& local, bool localOnly) const {
    return placed(*this, local, 1.0, localOnly);
}

JPLocation JPPlacementsHolderLocation::childLocation(const JPPlacementsHolderLocation& child, bool localOnly) const {
    JPLocation dummy(JPLengthUnit::Millimeters);
    if (child.globalSide() == JPSide::Bottom && child.holder)
        dummy = child.holder->dimensions.derive(std::nullopt, 0.0, 0.0, 0.0);
    const double angleSign = globalSide() == JPSide::Bottom ? -1.0 : 1.0;
    return placed(*this, child.placementLocation(dummy, true), angleSign, localOnly);
}

JPLocation JPPlacementsHolderLocation::placementLocationInverse(const JPLocation& global) const {
    JPAffineTransform t = localToGlobalTransform();
    if (const auto inv = t.inverse()) t = *inv;
    const double angleSign = globalSide() == JPSide::Bottom ? -1.0 : 1.0;
    const JPLengthUnit units = global.units();
    const JPLocation board = globalLocation().convertToUnits(JPLengthUnit::Millimeters);
    const JPLocation p = global.convertToUnits(JPLengthUnit::Millimeters);
    const double angle = t.info().rotationAngleDeg;
    double x, y;
    t.apply(p.x(), p.y(), x, y);
    return JPLocation(JPLengthUnit::Millimeters, x, y, p.z() - board.z(), p.rotation() + angleSign * angle).convertToUnits(units);
}

JPXmlNode JPPlacementsHolderLocation::toXml() const {
    JPXmlNode n("object");
    n.attr("class", kind() == Kind::Board ? kBoardLocationClass : kPanelLocationClass)
        .attr("side", JPSides::name(side))
        .attr("id", id);
    if (!fileName.empty()) n.attr("file-name", fileName);
    n.attr("check-fiducials", V::boolean(checkFiducials)).attr("locally-enabled", V::boolean(locallyEnabled));
    n.add(JPLocationXml::to("location", m_location));
    return n;
}

std::unique_ptr<JPPlacementsHolderLocation> JPPlacementsHolderLocation::fromXml(const JPXmlElement& e) {
    std::unique_ptr<JPPlacementsHolderLocation> l;
    const std::string cls = e.attr("class");
    if (cls == kPanelLocationClass) l = std::make_unique<JPPanelLocation>();
    else {
        auto b = std::make_unique<JPBoardLocation>();
        // An older job's board: its file, enabled, and what was placed.
        if (const JPXmlElement* placed = e.child("placed"))
            for (const JPXmlElement& entry : placed->children)
                if (entry.children.size() >= 2)
                    b->legacyPlaced[V::text(entry.children[0])] = V::text(entry.children[1]) == "true";
        l = std::move(b);
    }
    l->id = e.attr("id");
    l->side = JPSides::fromName(e.attr("side"));
    l->fileName = V::has(e, "file-name") ? e.attr("file-name") : e.attr("board-file");
    l->checkFiducials = V::boolean(e, "check-fiducials");
    l->locallyEnabled = V::has(e, "locally-enabled") ? V::boolean(e, "locally-enabled", true) : V::boolean(e, "enabled", true);
    if (const JPXmlElement* loc = e.child("location")) l->m_location = JPLocationXml::from(*loc);
    return l;
}

} // inline namespace jf
