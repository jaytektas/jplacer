// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPJob.h"

#include "JPXmlValues.h"

inline namespace jf {

using V = JPXmlValues;

namespace {

constexpr const char* kVersion = "2.0";
const std::string kDelimiter = JPPlacementsHolderLocation::kIdDelimiter;

std::string key(const JPPlacementsHolderLocation& l, const std::string& placementId) {
    return l.uniqueId() + kDelimiter + placementId;
}

void readMap(const JPXmlElement* m, std::map<std::string, bool>& out) {
    if (!m) return;
    for (const JPXmlElement& e : m->children)
        if (e.children.size() >= 2) out[V::text(e.children[0])] = V::text(e.children[1]) == "true";
}

JPXmlNode writeMap(const char* name, const std::map<std::string, bool>& m) {
    JPXmlNode n(name);
    n.attr("class", "java.util.HashMap");
    for (const auto& [k, v] : m) {
        JPXmlNode& e = n.add(JPXmlNode("entry"));
        e.add(JPXmlNode("string")).text = k;
        e.add(JPXmlNode("boolean")).text = V::boolean(v);
    }
    return n;
}

} // namespace

JPJob::JPJob() : m_rootPanel(std::make_shared<JPPanel>()), m_root(std::make_unique<JPPanelLocation>()) {
    m_root->holder = m_rootPanel;
    m_root->setLocalToParentTransform(JPAffineTransform());
    m_root->checkFiducials = false;
}

std::vector<JPBoardLocation*> JPJob::boardLocations() const {
    return m_rootPanel->descendantBoardLocations();
}

std::vector<JPPanelLocation*> JPJob::panelLocations() const {
    std::vector<JPPanelLocation*> out { m_root.get() };
    const auto more = m_rootPanel->descendantPanelLocations();
    out.insert(out.end(), more.begin(), more.end());
    return out;
}

std::vector<JPPlacementsHolderLocation*> JPJob::boardAndPanelLocations() const {
    std::vector<JPPlacementsHolderLocation*> out { m_root.get() };
    const auto more = m_rootPanel->descendants();
    out.insert(out.end(), more.begin(), more.end());
    return out;
}

JPPlacementsHolderLocation* JPJob::addBoardOrPanelLocation(std::unique_ptr<JPPlacementsHolderLocation> l) {
    dirty = true;
    return m_root->addChild(std::move(l));
}

void JPJob::removeBoardOrPanelLocation(const JPPlacementsHolderLocation* l) {
    dirty = true;
    m_root->removeChild(l);
}

int JPJob::instanceCount(const JPPlacementsHolder& h) const {
    return m_rootPanel->instanceCount(h);
}

int JPJob::totalActivePlacements(const JPPlacementsHolderLocation* l) const {
    if (!l || !l->holder || !l->isEnabled()) return 0;
    int n = 0;
    if (l->kind() == JPPlacementsHolderLocation::Kind::Board) {
        for (const JPPlacement& p : l->holder->placements)
            if (p.side == l->globalSide() && p.type == JPPlacement::Type::Placement && p.enabled) ++n;
    } else {
        for (const auto& c : static_cast<const JPPanel&>(*l->holder).children) n += totalActivePlacements(c.get());
    }
    return n;
}

int JPJob::activePlacements(const JPPlacementsHolderLocation* l) const {
    if (!l || !l->holder || !l->isEnabled()) return 0;
    int n = 0;
    if (l->kind() == JPPlacementsHolderLocation::Kind::Board) {
        for (const JPPlacement& p : l->holder->placements)
            if (p.side == l->globalSide() && p.type == JPPlacement::Type::Placement && p.enabled
                && !retrievePlacedStatus(*l, p.id))
                ++n;
    } else {
        for (const auto& c : static_cast<const JPPanel&>(*l->holder).children) n += activePlacements(c.get());
    }
    return n;
}

void JPJob::storePlacedStatus(const JPPlacementsHolderLocation& l, const std::string& placementId, bool placed) {
    placedStatusMap[key(l, placementId)] = placed;
}

bool JPJob::retrievePlacedStatus(const JPPlacementsHolderLocation& l, const std::string& placementId) const {
    const auto it = placedStatusMap.find(key(l, placementId));
    return it != placedStatusMap.end() && it->second;
}

void JPJob::removePlacedStatus(const JPPlacementsHolderLocation& l, const std::string& placementId) {
    placedStatusMap.erase(key(l, placementId));
}

void JPJob::storeEnabledState(const JPPlacementsHolderLocation& l, const JPPlacement* p, bool enabled) {
    enabledStateMap[p ? key(l, p->id) : l.uniqueId()] = enabled;
}

bool JPJob::retrieveEnabledState(const JPPlacementsHolderLocation& l, const JPPlacement* p) const {
    const auto it = enabledStateMap.find(p ? key(l, p->id) : l.uniqueId());
    if (it != enabledStateMap.end()) return it->second;
    return p ? p->enabled : l.locallyEnabled;
}

void JPJob::storeCheckFiducialsState(const JPPlacementsHolderLocation& l, bool check) {
    checkFiducialsStateMap[l.uniqueId()] = check;
}

bool JPJob::retrieveCheckFiducialsState(const JPPlacementsHolderLocation& l) const {
    const auto it = checkFiducialsStateMap.find(l.uniqueId());
    return it != checkFiducialsStateMap.end() ? it->second : l.checkFiducials;
}

void JPJob::storeErrorHandlingState(const JPPlacementsHolderLocation& l, const JPPlacement& p, JPPlacement::ErrorHandling e) {
    errorHandlingStateMap[key(l, p.id)] = e;
}

JPPlacement::ErrorHandling JPJob::retrieveErrorHandlingState(const JPPlacementsHolderLocation& l, const JPPlacement& p) const {
    const auto it = errorHandlingStateMap.find(key(l, p.id));
    return it != errorHandlingStateMap.end() ? it->second : p.errorHandling;
}

JPPlacement::ErrorHandling JPJob::effectiveErrorHandling(const JPPlacement& p) const {
    if (p.errorHandling != JPPlacement::ErrorHandling::Default) return p.errorHandling;
    return errorHandling == ErrorHandling::Defer ? JPPlacement::ErrorHandling::Defer : JPPlacement::ErrorHandling::Alert;
}

std::unique_ptr<JPJob> JPJob::fromXml(const JPXmlElement& r) {
    auto job = std::make_unique<JPJob>();
    if (V::has(r, "version")) job->version = V::number(r, "version");
    if (const JPXmlElement* rp = r.child("root-panel")) {
        JPPanel panel = JPPanel::fromXml(*rp);
        job->m_rootPanel->dimensions = panel.dimensions;
        job->m_rootPanel->placements = std::move(panel.placements);
        job->m_rootPanel->pseudoPlacementIds = std::move(panel.pseudoPlacementIds);
        for (auto& c : panel.children) job->m_root->addChild(std::move(c));
    }
    // Version 1: boards in a list, perhaps one panel of columns and rows.
    if (const JPXmlElement* bls = r.child("board-locations"))
        for (const JPXmlElement& e : bls->children) {
            auto l = JPPlacementsHolderLocation::fromXml(e);
            job->legacyBoardLocations.emplace_back(static_cast<JPBoardLocation*>(l.release()));
        }
    if (const JPXmlElement* ps = r.child("panels"))
        for (const JPXmlElement& e : ps->children) job->legacyPanels.push_back(JPPanel::fromXml(e));
    readMap(r.child("placed-status-map"), job->placedStatusMap);
    if (const JPXmlElement* plan = r.child("jplacer-plan")) {
        job->planSort = plan->attr("sort");
        for (const JPXmlElement& e : plan->children)
            if (e.name == "part") job->planOrder.push_back(e.attr("id"));
    }
    readMap(r.child("enabled-state-map"), job->enabledStateMap);
    readMap(r.child("check-fiducials-state-map"), job->checkFiducialsStateMap);
    if (const JPXmlElement* m = r.child("error-handling-state-map"))
        for (const JPXmlElement& e : m->children)
            if (e.children.size() >= 2)
                job->errorHandlingStateMap[V::text(e.children[0])] = JPPlacement::errorHandlingFrom(V::text(e.children[1]));
    if (const JPXmlElement* e = r.child("error-handling"))
        job->errorHandling = V::text(*e) == "Defer" ? ErrorHandling::Defer : ErrorHandling::Alert;
    return job;
}

JPXmlNode JPJob::toXml() const {
    JPXmlNode n("openpnp-job");
    n.attr("version", kVersion);
    n.add(m_rootPanel->toXml("root-panel"));
    n.add(writeMap("placed-status-map", placedStatusMap));
    n.add(writeMap("enabled-state-map", enabledStateMap));
    n.add(writeMap("check-fiducials-state-map", checkFiducialsStateMap));
    JPXmlNode& eh = n.add(JPXmlNode("error-handling-state-map"));
    eh.attr("class", "java.util.HashMap");
    for (const auto& [k, v] : errorHandlingStateMap) {
        JPXmlNode& e = eh.add(JPXmlNode("entry"));
        e.add(JPXmlNode("string")).text = k;
        e.add(JPXmlNode("error-handling")).text = JPPlacement::errorHandlingName(v);
    }
    n.add(JPXmlNode("error-handling")).text = errorHandling == ErrorHandling::Defer ? "Defer" : "Alert";
    if (!planSort.empty() || !planOrder.empty()) {
        JPXmlNode& plan = n.add(JPXmlNode("jplacer-plan"));
        if (!planSort.empty()) plan.attr("sort", planSort);
        for (const std::string& id : planOrder) plan.add(JPXmlNode("part")).attr("id", id);
    }
    return n;
}

} // inline namespace jf
