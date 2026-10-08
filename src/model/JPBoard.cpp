// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPBoard.h"

#include "JPLocationJson.h"
#include "JPLocationXml.h"

#include "openpnp/JPXmlJson.h"

#include <algorithm>
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
    const std::string prefix = scopeName() + "/";
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
        if (p.state == JPBoardPart::State::Local && p.localPart && p.localPart == theirs.localPart) {
            p.localPart = std::make_shared<JPPart>(*theirs.localPart);
            if (p.localPart->id.rfind(prefix, 0) != 0) p.localPart->id = prefix + p.localPart->id;
            if (theirs.localPackage) {
                p.localPackage = std::make_shared<JPPackage>(*theirs.localPackage);
                if (p.localPackage->id.rfind(prefix, 0) != 0) p.localPackage->id = prefix + p.localPackage->id;
                p.localPart->packageId = p.localPackage->id;
            }
        }
        keys[theirs.key] = key;
    }
    return keys;
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
    if (j["solderPastePads"].isArray())
        for (const JJson& p : j["solderPastePads"].arr()) b.solderPastePads.push_back(JPBoardPad::fromXml(JPXmlJson::element(p)));
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
