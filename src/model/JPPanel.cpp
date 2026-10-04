// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPPanel.h"

#include "JPBoardLocation.h"
#include "JPLocationXml.h"
#include "JPPanelLocation.h"
#include "JPSides.h"
#include "JPXmlValues.h"

#include <algorithm>

inline namespace jf {

using V = JPXmlValues;

namespace {

constexpr const char* kVersion = "2.0";
const std::string kDelimiter = JPPlacementsHolderLocation::kIdDelimiter;

double normalizeAngle180(double a) {
    while (a >= 180) a -= 360;
    while (a < -180) a += 360;
    return a;
}

} // namespace

JPPanel::JPPanel(const JPPanel& o)
    : JPPlacementsHolder(o), pseudoPlacementIds(o.pseudoPlacementIds), legacy(o.legacy) {
    for (const auto& c : o.children) children.push_back(c->instance());
}

std::shared_ptr<JPPlacementsHolder> JPPanel::instance() const {
    auto p = std::make_shared<JPPanel>(*this);
    p->m_definition = definition();
    return p;
}

JPPlacementsHolderLocation* JPPanel::child(const std::string& id) {
    if (id.empty()) return nullptr;
    for (const auto& c : children)
        if (c->id == id) return c.get();
    return nullptr;
}

JPPlacementsHolderLocation* JPPanel::addChild(std::unique_ptr<JPPlacementsHolderLocation> c) {
    if (c->id.empty() || child(c->id)) {
        const char* prefix = c->kind() == JPPlacementsHolderLocation::Kind::Board ? JPBoardLocation::kIdPrefix
                                                                                  : JPPanelLocation::kIdPrefix;
        c->id = createId(prefix, [this](const std::string& id) { return child(id) != nullptr; });
    }
    children.push_back(std::move(c));
    return children.back().get();
}

void JPPanel::removeChild(const JPPlacementsHolderLocation* c) {
    const auto it = std::find_if(children.begin(), children.end(), [c](const auto& p) { return p.get() == c; });
    if (it == children.end()) return;
    const std::string prefix = (*it)->id + kDelimiter;
    std::erase_if(pseudoPlacementIds, [&prefix](const std::string& id) { return id.rfind(prefix, 0) == 0; });
    children.erase(it);
}

JPPlacementsHolderLocation* JPPanel::replaceChild(const JPPlacementsHolderLocation* original,
                                                  std::unique_ptr<JPPlacementsHolderLocation> replacement) {
    const auto it = std::find_if(children.begin(), children.end(), [original](const auto& p) { return p.get() == original; });
    if (it == children.end()) return nullptr;
    replacement->id = original->id;
    replacement->checkFiducials = original->checkFiducials;
    replacement->setLocation(original->location());
    replacement->locallyEnabled = original->locallyEnabled;
    replacement->side = original->side;
    replacement->parent = original->parent;
    *it = std::move(replacement);
    // Pseudo-placements from it that the new board or panel does not have go.
    const std::string prefix = (*it)->id + kDelimiter;
    std::erase_if(pseudoPlacementIds, [this, &prefix](const std::string& id) {
        JPPlacement p;
        return id.rfind(prefix, 0) == 0 && !pseudoPlacementLocation(id, p);
    });
    return it->get();
}

std::vector<JPPlacementsHolderLocation*> JPPanel::descendants() const {
    std::vector<JPPlacementsHolderLocation*> out;
    for (const auto& c : children) {
        out.push_back(c.get());
        if (c->kind() == JPPlacementsHolderLocation::Kind::Panel && c->holder) {
            const auto more = static_cast<const JPPanel&>(*c->holder).descendants();
            out.insert(out.end(), more.begin(), more.end());
        }
    }
    return out;
}

std::vector<JPBoardLocation*> JPPanel::descendantBoardLocations() const {
    std::vector<JPBoardLocation*> out;
    for (const auto& c : children) {
        if (c->kind() == JPPlacementsHolderLocation::Kind::Board) out.push_back(static_cast<JPBoardLocation*>(c.get()));
        else if (c->holder) {
            const auto more = static_cast<const JPPanel&>(*c->holder).descendantBoardLocations();
            out.insert(out.end(), more.begin(), more.end());
        }
    }
    return out;
}

std::vector<JPPanelLocation*> JPPanel::descendantPanelLocations() const {
    std::vector<JPPanelLocation*> out;
    for (const auto& c : children) {
        if (c->kind() != JPPlacementsHolderLocation::Kind::Panel) continue;
        out.push_back(static_cast<JPPanelLocation*>(c.get()));
        if (c->holder) {
            const auto more = static_cast<const JPPanel&>(*c->holder).descendantPanelLocations();
            out.insert(out.end(), more.begin(), more.end());
        }
    }
    return out;
}

std::pair<std::vector<JPPlacementsHolderLocation*>, const JPPlacement*>
JPPanel::descendantPlacement(const std::string& uniqueId) const {
    for (const auto& c : children) {
        if (c->id.empty() || uniqueId.rfind(c->id, 0) != 0 || !c->holder) continue;
        std::string rest = uniqueId.substr(c->id.size());
        if (rest.rfind(kDelimiter, 0) == 0) rest = rest.substr(kDelimiter.size());
        if (const JPPlacement* p = c->holder->find(rest)) return { { c.get() }, p };
        if (c->kind() == JPPlacementsHolderLocation::Kind::Panel) {
            auto r = static_cast<const JPPanel&>(*c->holder).descendantPlacement(rest);
            if (r.second) {
                r.first.insert(r.first.begin(), c.get());
                return r;
            }
        }
    }
    return { {}, nullptr };
}

int JPPanel::instanceCount(const JPPlacementsHolder& h) const {
    int n = 0;
    for (const auto& c : children) {
        if (!c->holder) continue;
        if (c->holder->definition() == h.definition()) ++n;
        else if (c->kind() == JPPlacementsHolderLocation::Kind::Panel)
            n += static_cast<const JPPanel&>(*c->holder).instanceCount(h);
    }
    return n;
}

bool JPPanel::isDefinitionUsed(const JPPlacementsHolder& h) const {
    if (definition() == h.definition()) return true;
    for (const JPPlacementsHolderLocation* d : descendants()) {
        if (!d->holder) continue;
        if (d->holder->definition() == h.definition()) return true;
        if (d->kind() == JPPlacementsHolderLocation::Kind::Panel && static_cast<const JPPanel&>(*d->holder).isDefinitionUsed(h))
            return true;
    }
    return false;
}

bool JPPanel::pseudoPlacementLocation(const std::string& id, JPPlacement& out) const {
    const auto [branches, leaf] = descendantPlacement(id);
    if (!leaf || branches.empty()) return false;
    // As OpenPnP: the leaf's location through every board and panel on the
    // way, in this panel's coordinates; its side as the tip shows it.
    JPAffineTransform t;
    JPSide tipSide = JPSide::Top;
    for (size_t i = 0; i < branches.size(); ++i) {
        t.concatenate(branches[i]->localToParentTransform());
        tipSide = i == 0 ? branches[i]->side : JPSides::flip(branches[i]->side, tipSide == JPSide::Bottom);
    }
    const JPLocation p = leaf->location.convertToUnits(JPLengthUnit::Millimeters);
    double x, y;
    t.apply(p.x(), p.y(), x, y);
    JPLocation location(JPLengthUnit::Millimeters, x, y, 0.0, t.info().rotationAngleDeg + p.rotation());
    location = location.convertToUnits(leaf->location.units());
    if (tipSide != leaf->side)
        location = location.derive(std::nullopt, std::nullopt, std::nullopt,
                                   normalizeAngle180(-location.rotation() + 2 * leaf->location.rotation()));
    out = JPPlacement();
    out.id = id;
    out.enabled = true;
    out.partId = leaf->partId;
    out.type = leaf->type;
    out.location = location;
    out.side = JPSides::flip(leaf->side, tipSide == JPSide::Bottom);
    out.comments = leaf->type == JPPlacement::Type::Placement ? "Pseudo-placement, for panel alignment only"
                                                              : "Pseudo-fiducial, for panel alignment only";
    return true;
}

std::vector<JPPlacement> JPPanel::pseudoPlacements() const {
    std::vector<JPPlacement> out;
    for (const std::string& id : pseudoPlacementIds) {
        JPPlacement p;
        if (pseudoPlacementLocation(id, p)) {
            p.enabled = !disabledPseudoPlacements.count(id);
            out.push_back(std::move(p));
        }
    }
    return out;
}

JPPanel JPPanel::fromXml(const JPXmlElement& root) {
    JPPanel p;
    if (V::has(root, "name")) p.name = root.attr("name");
    if (const JPXmlElement* d = root.child("dimensions")) p.dimensions = JPLocationXml::from(*d);
    if (const JPXmlElement* ps = root.child("placements"))
        for (const JPXmlElement& e : ps->children)
            if (e.name == "placement") p.placements.push_back(JPPlacement::fromXml(e));
    if (const JPXmlElement* pr = root.child("profile")) p.profile = JPProfile::fromXml(*pr);
    if (const JPXmlElement* cs = root.child("children"))
        for (const JPXmlElement& e : cs->children) p.children.push_back(JPPlacementsHolderLocation::fromXml(e));
    if (const JPXmlElement* ids = root.child("pseudo-placement-ids"))
        for (const JPXmlElement& e : ids->children) p.pseudoPlacementIds.push_back(V::text(e));
    // An older job's panel: columns and rows of its first board.
    if (root.child("columns") || root.child("rows") || root.child("fiducials")) {
        Legacy l;
        if (const JPXmlElement* e = root.child("id")) l.id = V::text(*e);
        if (const JPXmlElement* e = root.child("columns")) l.columns = std::max(1, std::atoi(V::text(*e).c_str()));
        if (const JPXmlElement* e = root.child("rows")) l.rows = std::max(1, std::atoi(V::text(*e).c_str()));
        if (const JPXmlElement* e = root.child("x-gap")) l.xGap = JPLocationXml::lengthFrom(*e);
        if (const JPXmlElement* e = root.child("y-gap")) l.yGap = JPLocationXml::lengthFrom(*e);
        if (const JPXmlElement* e = root.child("part-id")) l.partId = V::text(*e);
        if (const JPXmlElement* e = root.child("check-fids")) l.checkFids = V::text(*e) == "true";
        if (const JPXmlElement* fs = root.child("fiducials"))
            for (const JPXmlElement& e : fs->children)
                if (e.name == "placement") l.fiducials.push_back(JPPlacement::fromXml(e));
        // As OpenPnP: the panel's fiducials are its placements.
        p.placements.insert(p.placements.end(), l.fiducials.begin(), l.fiducials.end());
        p.legacy = std::move(l);
    }
    return p;
}

JPXmlNode JPPanel::toXml(const char* elementName) const {
    JPXmlNode n(elementName);
    if (name) n.attr("name", *name);
    n.attr("version", kVersion);
    n.add(JPLocationXml::to("dimensions", dimensions));
    JPXmlNode& ps = n.add(JPXmlNode("placements"));
    for (const JPPlacement& p : placements) ps.add(p.toXml());
    if (profile) n.add(profile->toXml());
    JPXmlNode& cs = n.add(JPXmlNode("children"));
    for (const auto& c : children) cs.add(c->toXml());
    JPXmlNode& ids = n.add(JPXmlNode("pseudo-placement-ids"));
    for (const std::string& id : pseudoPlacementIds) ids.add(JPXmlNode("string")).text = id;
    return n;
}

} // inline namespace jf
