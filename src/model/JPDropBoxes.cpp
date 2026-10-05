// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPDropBoxes.h"

#include "JPLocationXml.h"
#include "JPOpenPnpIds.h"

#include <algorithm>
#include <cctype>

inline namespace jf {

namespace {

constexpr const char* kPropertyClass = "org.openpnp.machine.reference.feeder.ReferenceHeapFeeder$DropBoxProperty";
// What OpenPnP names the dummy part when none is chosen.
constexpr const char* kDummyPart = "HeapFedder-Dummy";

JPXmlElement element(const JPXmlNode& n) {
    JPXmlElement e;
    e.name = n.name;
    for (const auto& [k, v] : n.attributes) e.attributes[k] = v;
    return e;
}

} // namespace

JPDropBoxes::JPDropBoxes() {
    JPXmlNode name("string");
    name.text = kKey;
    m_entry.add(std::move(name));
    JPXmlNode object("object");
    object.attr("class", kPropertyClass);
    object.add(JPXmlNode("boxes"));
    m_entry.add(std::move(object));
    setName(add(), "Green");
}

std::optional<JPDropBoxes> JPDropBoxes::fromXml(const JPXmlElement& entry) {
    const JPXmlElement* name = entry.child("string");
    if (!name || name->text != kKey) return std::nullopt;
    JPDropBoxes b;
    b.m_entry = JPXmlNode::from(entry);
    if (!b.boxesNode()) {
        JPXmlNode* object = b.m_entry.child("object");
        if (!object) object = &b.m_entry.add(JPXmlNode("object"));
        object->add(JPXmlNode("boxes"));
    }
    if (b.boxes().empty()) b.setName(b.add(), "Green");
    return b;
}

JPXmlNode* JPDropBoxes::boxesNode() {
    JPXmlNode* object = m_entry.child("object");
    return object ? object->child("boxes") : nullptr;
}

const JPXmlNode* JPDropBoxes::boxesNode() const {
    const JPXmlNode* object = m_entry.child("object");
    return object ? object->child("boxes") : nullptr;
}

JPXmlNode* JPDropBoxes::boxNode(const std::string& id) {
    if (JPXmlNode* boxes = boxesNode())
        for (JPXmlNode& b : boxes->children)
            if (const std::string* i = b.get("id"); i && *i == id) return &b;
    return nullptr;
}

const JPXmlNode* JPDropBoxes::boxNode(const std::string& id) const {
    if (const JPXmlNode* boxes = boxesNode())
        for (const JPXmlNode& b : boxes->children)
            if (const std::string* i = b.get("id"); i && *i == id) return &b;
    return nullptr;
}

std::vector<JPDropBoxes::Box> JPDropBoxes::boxes() const {
    std::vector<Box> out;
    if (const JPXmlNode* boxes = boxesNode())
        for (const JPXmlNode& b : boxes->children) {
            const std::string* id = b.get("id");
            if (!id) continue;
            Box box;
            box.id = *id;
            box.name = b.get("name") ? *b.get("name") : *id;
            box.dummyPartId = b.get("dummy-part-id-for-unknown") ? *b.get("dummy-part-id-for-unknown") : kDummyPart;
            if (const JPXmlNode* l = b.child("center-bottom-location")) box.centerBottom = JPLocationXml::from(element(*l));
            if (const JPXmlNode* l = b.child("drop-location")) box.drop = JPLocationXml::from(element(*l));
            out.push_back(std::move(box));
        }
    return out;
}

std::optional<JPDropBoxes::Box> JPDropBoxes::box(const std::string& id) const {
    for (const Box& b : boxes())
        if (b.id == id) return b;
    return std::nullopt;
}

std::string JPDropBoxes::add() {
    const std::string id = JPOpenPnpIds::create("DropBox-");
    JPXmlNode box("drop-box");
    box.attr("id", id).attr("name", id).attr("dummy-part-id-for-unknown", kDummyPart);
    box.add(JPLocationXml::to("center-bottom-location", JPLocation(JPLengthUnit::Millimeters)));
    box.add(JPLocationXml::to("drop-location", JPLocation(JPLengthUnit::Millimeters)));
    if (JPXmlNode* boxes = boxesNode()) boxes->add(std::move(box));
    return id;
}

bool JPDropBoxes::remove(const std::string& id, std::string& why) {
    JPXmlNode* boxes = boxesNode();
    if (!boxes || boxes->children.size() < 2) {
        why = "Can't delete the only DropBox. There must always be one DropBox defined.";
        return false;
    }
    std::erase_if(boxes->children, [&id](const JPXmlNode& b) {
        const std::string* i = b.get("id");
        return i && *i == id;
    });
    lastHeap.erase(id);
    return true;
}

void JPDropBoxes::setName(const std::string& id, const std::string& name) {
    if (JPXmlNode* b = boxNode(id)) b->set("name", name);
}

void JPDropBoxes::setDummyPart(const std::string& id, const std::string& partId) {
    if (JPXmlNode* b = boxNode(id)) b->set("dummy-part-id-for-unknown", partId.empty() ? kDummyPart : partId);
}

void JPDropBoxes::setCenterBottom(const std::string& id, const JPLocation& l) {
    if (JPXmlNode* b = boxNode(id)) {
        if (JPXmlNode* c = b->child("center-bottom-location")) *c = JPLocationXml::to("center-bottom-location", l);
        else b->add(JPLocationXml::to("center-bottom-location", l));
    }
}

void JPDropBoxes::setDrop(const std::string& id, const JPLocation& l) {
    if (JPXmlNode* b = boxNode(id)) {
        if (JPXmlNode* c = b->child("drop-location")) *c = JPLocationXml::to("drop-location", l);
        else b->add(JPLocationXml::to("drop-location", l));
    }
}

const JPXmlNode* JPDropBoxes::partPipeline(const std::string& id) const {
    const JPXmlNode* b = boxNode(id);
    return b ? b->child("part-pipeline") : nullptr;
}

void JPDropBoxes::setPartPipeline(const std::string& id, JPXmlNode pipeline) {
    JPXmlNode* b = boxNode(id);
    if (!b) return;
    pipeline.name = "part-pipeline";
    if (JPXmlNode* c = b->child("part-pipeline")) *c = std::move(pipeline);
    else b->add(std::move(pipeline));
}

std::string JPDropBoxes::colour(const std::string& name) {
    std::string upper = name;
    std::transform(upper.begin(), upper.end(), upper.begin(), [](unsigned char c) { return char(std::toupper(c)); });
    return upper == "WHITE" || upper == "BLACK" ? upper : "GREEN";
}

} // inline namespace jf
