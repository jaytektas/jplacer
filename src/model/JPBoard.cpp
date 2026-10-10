// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPBoard.h"

#include "JPAngles.h"
#include "JPLocationJson.h"
#include "JPLocationXml.h"

#include "common/JPUuid.h"
#include "openpnp/JPXmlJson.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <set>

inline namespace jf {

namespace {
constexpr const char* kVersion = "1.1";
}

std::shared_ptr<JPPlacementsHolder> JPBoard::instance() const {
    auto b = std::make_shared<JPBoard>(*this);
    b->m_definition = definition();
    return b;
}

JPBoard JPBoard::fromXml(const JPXmlElement& root) {
    JPBoard b;
    if (root.attributes.count("name")) b.name = root.attr("name");
    if (const JPXmlElement* d = root.child("dimensions")) b.dimensions = JPLocationXml::from(*d);
    if (const JPXmlElement* ps = root.child("placements"))
        for (const JPXmlElement& p : ps->children)
            if (p.name == "placement") b.placements.push_back(JPPlacement::fromXml(p));
    if (const JPXmlElement* p = root.child("profile")) b.profile = JPProfile::fromXml(*p);
    if (const JPXmlElement* pads = root.child("solder-paste-pads"))
        for (const JPXmlElement& p : pads->children) b.solderPastePads.push_back(JPBoardPad::fromXml(p));
    return b;
}

JPXmlNode JPBoard::toXml() const {
    JPXmlNode n("openpnp-board");
    n.attr("version", kVersion);
    if (name) n.attr("name", *name);
    n.add(JPLocationXml::to("dimensions", dimensions));
    JPXmlNode& ps = n.add(JPXmlNode("placements"));
    for (const JPPlacement& p : placements) ps.add(p.toXml());
    if (profile) n.add(profile->toXml());
    n.add(JPXmlNode("fiducials"));
    JPXmlNode& pads = n.add(JPXmlNode("solder-paste-pads"));
    for (const JPBoardPad& p : solderPastePads) pads.add(p.toXml());
    return n;
}

JPBoardPart* JPBoard::part(const std::string& key) {
    for (JPBoardPart& p : *m_parts)
        if (p.key == key) return &p;
    return nullptr;
}

const JPBoardPart* JPBoard::part(const std::string& key) const {
    for (const JPBoardPart& p : *m_parts)
        if (p.key == key) return &p;
    return nullptr;
}

std::string JPBoard::newPartKey() const {
    return createId("bp-", [this](const std::string& k) { return part(k) != nullptr; });
}

std::string JPBoard::useLibraryPart(const std::string& libraryId) {
    for (const JPBoardPart& p : *m_parts)
        if (p.state == JPBoardPart::State::Matched && p.libraryPartId == libraryId) return p.key;
    JPBoardPart p;
    p.key = newPartKey();
    p.fields["part"] = libraryId;
    p.state = JPBoardPart::State::Matched;
    p.libraryPartId = libraryId;
    m_parts->push_back(p);
    return p.key;
}

std::map<std::string, std::string> JPBoard::takeParts(const JPBoard& from) {
    std::map<std::string, std::string> keys;
    for (const JPBoardPart& theirs : from.parts()) {
        JPBoardPart* mine = nullptr;
        for (JPBoardPart& p : *m_parts)
            if (!theirs.field("part").empty() && p.field("part") == theirs.field("part")) {
                mine = &p;
                break;
            }
        std::string key;
        if (mine) {
            // What the file says now is kept; a choice made here is not undone by importing again, but one
            // not yet made takes the import's.
            key = mine->key;
            for (const auto& [k, v] : theirs.fields) mine->fields[k] = v;
            if (mine->state == JPBoardPart::State::Unmatched && theirs.state != JPBoardPart::State::Unmatched) {
                mine->state = theirs.state;
                mine->libraryPartId = theirs.libraryPartId;
                mine->localPart = theirs.localPart;
                mine->localPackage = theirs.localPackage;
            }
        } else {
            JPBoardPart p = theirs;
            p.key = key = newPartKey();
            m_parts->push_back(p);
        }
        // A board's own part taken from the import: a copy of its own, its ids scoped to this board.
        JPBoardPart& p = *part(key);
        if (p.state == JPBoardPart::State::Local && p.localPart && p.localPart == theirs.localPart) scopeOwn(p);
        keys[theirs.key] = key;
    }
    return keys;
}

void JPBoard::scopeOwn(JPBoardPart& p) const {
    if (p.state != JPBoardPart::State::Local || !p.localPart) return;
    const std::string prefix = scopeName() + "/";
    p.localPart = std::make_shared<JPPart>(*p.localPart);
    if (p.localPart->id.rfind(prefix, 0) != 0) p.localPart->id = prefix + p.localPart->id;
    if (p.localPackage) {
        p.localPackage = std::make_shared<JPPackage>(*p.localPackage);
        if (p.localPackage->id.rfind(prefix, 0) != 0) p.localPackage->id = prefix + p.localPackage->id;
        p.localPart->packageId = p.localPackage->id;
    }
}

std::string JPBoard::scopeName() const {
    std::string s = !file.empty() ? std::filesystem::path(file).filename().string() : name.value_or("board");
    for (const char* suffix : { kExtension, ".board.xml", ".xml" }) {
        const std::string x = suffix;
        if (s.size() > x.size() && s.compare(s.size() - x.size(), x.size(), x) == 0) {
            s.resize(s.size() - x.size());
            break;
        }
    }
    return s;
}

std::string JPBoard::matchPlacement(const std::string& placementId, const std::string& libraryId) {
    const JPPlacement* placement = find(placementId);
    JPBoardPart* old = placement ? part(placement->boardPart) : nullptr;
    if (!old) return useLibraryPart(libraryId);
    if (old->state == JPBoardPart::State::Matched && old->libraryPartId == libraryId) return old->key;
    int uses = 0;
    for (const JPPlacement& p : placements) uses += p.boardPart == old->key;
    JPBoardPart* to = old;
    if (uses > 1) {
        JPBoardPart split = *old;
        split.key = newPartKey();
        m_parts->push_back(split);
        to = &m_parts->back();
    }
    to->state = JPBoardPart::State::Matched;
    to->libraryPartId = libraryId;
    to->localPart.reset();
    to->localPackage.reset();
    return to->key;
}

std::string JPBoard::splitPlacement(const std::string& placementId) {
    const JPPlacement* placement = find(placementId);
    if (!placement) return "";
    JPBoardPart* own = part(placement->boardPart);
    if (own && placementsOf(own->key).size() == 1) return own->key;
    JPBoardPart split = own ? *own : JPBoardPart();
    split.key = newPartKey();
    if (!own) split.fields["part"] = placement->partId;
    m_parts->push_back(split);
    return split.key;
}

void JPBoard::makeOwn(const std::string& key, const JPPackage* libraryPackage) {
    JPBoardPart* bp = part(key);
    if (!bp) return;
    const std::string prefix = scopeName() + "/";
    const std::string footprint = !bp->field("footprint").empty() ? bp->field("footprint") : bp->field("package");
    std::string name = bp->field("part");
    if (name.empty()) name = !bp->field("mpn").empty() ? bp->field("mpn") : footprint + "-" + bp->field("value");
    bp->state = JPBoardPart::State::Local;
    bp->libraryPartId.clear();
    bp->localPart = std::make_shared<JPPart>();
    bp->localPart->id = name.rfind(prefix, 0) == 0 ? name : prefix + name;
    bp->localPart->value = bp->field("value");   // its value as the files wrote it
    bp->localPackage.reset();
    if (libraryPackage) {
        bp->localPart->packageId = libraryPackage->id;
    } else if (!footprint.empty()) {
        bp->localPackage = std::make_shared<JPPackage>();
        bp->localPackage->id = prefix + footprint;
        bp->localPart->packageId = bp->localPackage->id;
    }
    // Its height where the files gave one (millimetres; "1.2mm").
    if (const std::string h = bp->field("height"); !h.empty()) {
        char* end = nullptr;
        const double mm = std::strtod(h.c_str(), &end);
        if (end != h.c_str()) bp->localPart->height = JPLength(mm, JPLengthUnit::Millimeters);
    }
}

std::vector<std::string> JPBoard::placementsOf(const std::string& key) const {
    std::vector<std::string> out;
    for (const JPPlacement& p : placements)
        if (p.boardPart == key) out.push_back(p.id);
    return out;
}

void JPBoard::dropUnusedParts() {
    std::set<std::string> used;
    for (const JPPlacement& p : placements) used.insert(p.boardPart);
    std::erase_if(*m_parts, [&used](const JPBoardPart& p) { return !used.count(p.key); });
}

void JPBoard::syncParts() {
    for (JPPlacement& p : placements)
        if (const JPBoardPart* bp = part(p.boardPart)) p.partId = bp->partId();
}

void JPBoard::partsFromPlacements(const std::function<bool(const std::string&)>& inLibrary) {
    for (JPPlacement& p : placements) {
        if (p.partId.empty() || part(p.boardPart)) continue;
        std::string key;
        for (const JPBoardPart& bp : *m_parts)
            if (bp.field("part") == p.partId) key = bp.key;
        if (key.empty()) {
            JPBoardPart bp;
            bp.key = key = newPartKey();
            bp.fields["part"] = p.partId;
            if (inLibrary(p.partId)) {
                bp.state = JPBoardPart::State::Matched;
                bp.libraryPartId = p.partId;
            }
            m_parts->push_back(bp);
        }
        p.boardPart = key;
    }
    syncParts();
}

std::string JPBoard::revisionLabel() const {
    return m_revisions.empty() ? std::string() : m_revisions[m_revision].label;
}

JPBoardRevision JPBoard::revisionAt(size_t i) const {
    if (i >= m_revisions.size()) return {};
    JPBoardRevision r = m_revisions[i];
    if (i == m_revision) {
        r.provenance = provenance;
        r.placements = placements;
        r.parts.clear();
        for (const JPBoardPart& p : *m_parts) {
            JPBoardPart q = p;
            q.takeChoice(p);
            r.parts.push_back(q);
        }
    }
    return r;
}

void JPBoard::giveIdentities() {
    for (JPPlacement& p : placements)
        if (p.uid.empty()) p.uid = JPUuid::make();
}

void JPBoard::keepShown() {
    if (m_revision < m_revisions.size()) m_revisions[m_revision] = revisionAt(m_revision);
}

void JPBoard::show(size_t i) {
    JPBoardRevision& r = m_revisions[i];
    m_revision = i;
    provenance = std::move(r.provenance);
    placements = std::move(r.placements);
    *m_parts = std::move(r.parts);
    r.provenance.clear();
    r.placements.clear();
    r.parts.clear();
    syncParts();
}

void JPBoard::addRevision(JPBoardRevision r, const std::string& currentLabel) {
    giveIdentities();
    if (m_revisions.empty()) {
        JPBoardRevision first;
        first.label = currentLabel;
        m_revisions.push_back(first);
        m_revision = 0;
    }
    keepShown();
    m_revisions.push_back(std::move(r));
    show(m_revisions.size() - 1);
    dirty = true;
}

void JPBoard::showRevision(size_t i) {
    if (i >= m_revisions.size() || i == m_revision) return;
    keepShown();
    show(i);
}

size_t JPBoard::revisionNamed(const std::string& label) const {
    for (size_t i = 0; i < m_revisions.size(); ++i)
        if (m_revisions[i].label == label) return i;
    return m_revisions.size();
}

JPRevisionCarry JPBoard::switchRevision(size_t i) {
    JPRevisionCarry carry;
    if (i >= m_revisions.size() || i == m_revision) return carry;
    const JPBoardRevision from = revisionAt(m_revision);
    showRevision(i);
    dirty = true;
    carry.from = from.label;
    carry.before = revisionAt(i);
    std::map<std::string, const JPPlacement*> byUid;
    for (const JPPlacement& p : from.placements)
        if (!p.uid.empty()) byUid[p.uid] = &p;
    auto partIn = [](const std::vector<JPBoardPart>& parts, const std::string& key) -> const JPBoardPart* {
        for (const JPBoardPart& p : parts)
            if (p.key == key) return &p;
        return nullptr;
    };
    auto samePlace = [](const JPPlacement& a, const JPPlacement& b) {
        const JPLocation x = a.location.convertToUnits(JPLengthUnit::Millimeters);
        const JPLocation y = b.location.convertToUnits(JPLengthUnit::Millimeters);
        constexpr double kSame = 0.001;   // mm, degrees: the same number, as files round it
        if (std::abs(x.x() - y.x()) > kSame || std::abs(x.y() - y.y()) > kSame) return false;
        if (a.cadRotation.has_value() != b.cadRotation.has_value()) return false;
        return !a.cadRotation || std::abs(JPAngles::difference(*a.cadRotation, *b.cadRotation)) <= kSame;
    };
    // Each board part here, the one there its placements (the same) were given, and those placements.
    std::map<std::string, std::set<std::string>> partFrom;
    std::map<std::string, std::vector<std::string>> partPlacements;
    for (JPPlacement& t : placements) {
        const auto s = byUid.find(t.uid);
        if (t.uid.empty() || s == byUid.end()) continue;
        const JPPlacement& was = *s->second;
        const JPBoardPart* sp = partIn(from.parts, was.boardPart);
        const JPBoardPart* tp = part(t.boardPart);
        if (was.side != t.side || !samePlace(was, t) || !sp || !tp || !sp->samePart(*tp)) continue;
        // Work newer than what is here: verified there, and here not verified or verified before (JPWhen's
        // times sort as they read).
        if (!was.verified.by.empty() && (t.verified.by.empty() || t.verified.when < was.verified.when)) {
            if (std::abs(JPAngles::difference(was.location.rotation(), t.location.rotation())) > 1e-6) {
                t.location = t.location.derive(std::nullopt, std::nullopt, std::nullopt, was.location.rotation());
                t.verified = was.verified;
                carry.rotations.push_back(t.id);
            } else {
                t.verified = was.verified;
                carry.verified.push_back(t.id);
            }
        }
        partFrom[tp->key].insert(sp->key);
        partPlacements[tp->key].push_back(t.id);
    }
    for (const auto& [key, fromKeys] : partFrom) {
        const JPBoardPart* sp = fromKeys.size() == 1 ? partIn(from.parts, *fromKeys.begin()) : nullptr;
        JPBoardPart* tp = part(key);
        if (!sp || !tp || sp->state == JPBoardPart::State::Unmatched || sp->partId() == tp->partId()) continue;
        tp->takeChoice(*sp);
        for (const std::string& id : partPlacements[key]) carry.parts.push_back(id);
    }
    syncParts();
    return carry;
}

void JPBoard::undoCarry(const JPRevisionCarry& carry) {
    placements = carry.before.placements;
    *m_parts = carry.before.parts;
    syncParts();
    dirty = true;
}

void JPBoard::followRevision(const JPBoard& def) {
    std::map<std::string, const JPPlacement*> mine;
    for (const JPPlacement& p : placements) mine[!p.uid.empty() ? p.uid : p.id] = &p;
    std::vector<JPPlacement> shown = def.placements;
    for (JPPlacement& p : shown)
        if (const auto m = mine.find(!p.uid.empty() ? p.uid : p.id); m != mine.end()) {
            p.enabled = m->second->enabled;
            p.errorHandling = m->second->errorHandling;
        }
    placements = std::move(shown);
    provenance = def.provenance;
    m_revisions = def.m_revisions;
    m_revision = def.m_revision;
    syncParts();
}

JPBoard JPBoard::fromJson(const JJson& j) {
    JPBoard b;
    if (j["name"].isString()) b.name = j["name"].str();
    if (j["convertedFrom"].isString()) b.convertedFrom = j["convertedFrom"].str();
    if (j["dimensions"].isObject()) b.dimensions = JPLocationJson::from(j["dimensions"]);
    if (j["parts"].isArray())
        for (const JJson& p : j["parts"].arr()) b.m_parts->push_back(JPBoardPart::fromJson(p));
    if (j["placements"].isArray())
        for (const JJson& p : j["placements"].arr()) b.placements.push_back(JPPlacement::fromJson(p));
    if (j["profile"].isObject()) b.profile = JPProfile::fromXml(JPXmlJson::element(j["profile"]));
    if (j["provenance"].isArray())
        for (const JJson& p : j["provenance"].arr()) b.provenance.push_back(p);
    if (j["solderPastePads"].isArray())
        for (const JJson& p : j["solderPastePads"].arr()) b.solderPastePads.push_back(JPBoardPad::fromXml(JPXmlJson::element(p)));
    // Its revisions: the one shown is the board's own placements and parts.
    if (j["revisions"].isArray())
        for (const JJson& r : j["revisions"].arr()) {
            if (r["shown"].boolean(false)) b.m_revision = b.m_revisions.size();
            b.m_revisions.push_back(JPBoardRevision::fromJson(r));
        }
    b.syncParts();
    return b;
}

JJson JPBoard::toJson() const {
    JJson j = JJson::object();
    j["format"] = kFormat;
    j["version"] = 1;
    if (name) j["name"] = *name;
    if (!convertedFrom.empty()) j["convertedFrom"] = convertedFrom;
    j["dimensions"] = JPLocationJson::to(dimensions);
    JJson parts = JJson::array();
    for (const JPBoardPart& p : *m_parts) parts.push(p.toJson());
    j["parts"] = parts;
    JJson ps = JJson::array();
    for (const JPPlacement& p : placements) ps.push(p.toJson());
    j["placements"] = ps;
    if (profile) j["profile"] = JPXmlJson::from(profile->toXml());
    if (!provenance.empty()) {
        JJson sources = JJson::array();
        for (const JJson& p : provenance) sources.push(p);
        j["provenance"] = sources;
    }
    if (!m_revisions.empty()) {
        JJson revs = JJson::array();
        for (size_t i = 0; i < m_revisions.size(); ++i) {
            JJson r = m_revisions[i].toJson(i != m_revision);
            if (i == m_revision) r["shown"] = true;
            revs.push(r);
        }
        j["revisions"] = revs;
    }
    if (!solderPastePads.empty()) {
        JJson pads = JJson::array();
        for (const JPBoardPad& p : solderPastePads) pads.push(JPXmlJson::from(p.toXml()));
        j["solderPastePads"] = pads;
    }
    return j;
}

bool JPBoard::isJplacerFile(const std::string& path) {
    const std::string ext = kExtension;
    return path.size() > ext.size() && path.compare(path.size() - ext.size(), ext.size(), ext) == 0;
}

} // inline namespace jf
