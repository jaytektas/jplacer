// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPSlotBanks.h"

#include "JPLocationXml.h"
#include "JPOpenPnpIds.h"

inline namespace jf {

namespace {

constexpr const char* kAutoKey    = "ReferenceAutoFeederSlot.banks";
constexpr const char* kSchultzKey = "SchultzFeederSlot.banks";

const std::string* attr(const JPXmlNode& n, const std::string& key) { return n.get(key); }

JPXmlElement element(const JPXmlNode& n) {
    JPXmlElement e;
    e.name = n.name;
    for (const auto& [k, v] : n.attributes) e.attributes[k] = v;
    return e;
}

} // namespace

std::string JPSlotBanks::keyFor(const std::string& typeName) {
    if (typeName == "ReferenceSlotAutoFeeder") return kAutoKey;
    if (typeName == "SlotSchultzFeeder") return kSchultzKey;
    return {};
}

JPSlotBanks::JPSlotBanks(const std::string& key) {
    JPXmlNode name("string");
    name.text = key;
    m_entry.add(std::move(name));
    const std::string owner = key == kAutoKey ? "ReferenceSlotAutoFeeder" : "SlotSchultzFeeder";
    JPXmlNode object("object");
    object.attr("class", "org.openpnp.machine.reference.feeder." + owner + "$BanksProperty");
    object.add(JPXmlNode("banks"));
    m_entry.add(std::move(object));
    setBankName(addBank(), "Default");
}

std::optional<JPSlotBanks> JPSlotBanks::fromXml(const JPXmlElement& entry) {
    const JPXmlElement* name = entry.child("string");
    if (!name) return std::nullopt;
    const std::string key = name->text;
    if (key != kAutoKey && key != kSchultzKey) return std::nullopt;
    JPSlotBanks b(key);
    b.m_entry = JPXmlNode::from(entry);
    if (!b.banksNode()) {
        JPXmlNode* object = b.m_entry.child("object");
        if (!object) object = &b.m_entry.add(JPXmlNode("object"));
        object->add(JPXmlNode("banks"));
    }
    if (b.banks().empty()) b.setBankName(b.addBank(), "Default");
    return b;
}

std::string JPSlotBanks::key() const {
    const JPXmlNode* name = m_entry.child("string");
    return name ? name->text : std::string();
}

JPXmlNode* JPSlotBanks::banksNode() {
    JPXmlNode* object = m_entry.child("object");
    return object ? object->child("banks") : nullptr;
}

const JPXmlNode* JPSlotBanks::banksNode() const {
    const JPXmlNode* object = m_entry.child("object");
    return object ? object->child("banks") : nullptr;
}

JPXmlNode* JPSlotBanks::bankNode(const std::string& bankId) {
    if (JPXmlNode* banks = banksNode())
        for (JPXmlNode& b : banks->children)
            if (const std::string* id = attr(b, "id"); id && *id == bankId) return &b;
    return nullptr;
}

const JPXmlNode* JPSlotBanks::bankNode(const std::string& bankId) const {
    if (const JPXmlNode* banks = banksNode())
        for (const JPXmlNode& b : banks->children)
            if (const std::string* id = attr(b, "id"); id && *id == bankId) return &b;
    return nullptr;
}

JPXmlNode* JPSlotBanks::feederNode(const std::string& bankId, const std::string& feederId) {
    JPXmlNode* bank = bankNode(bankId);
    JPXmlNode* feeders = bank ? bank->child("feeders") : nullptr;
    if (feeders)
        for (JPXmlNode& f : feeders->children)
            if (const std::string* id = attr(f, "id"); id && *id == feederId) return &f;
    return nullptr;
}

std::vector<JPSlotBanks::Bank> JPSlotBanks::banks() const {
    std::vector<Bank> out;
    if (const JPXmlNode* banks = banksNode())
        for (const JPXmlNode& b : banks->children) {
            const std::string* id = attr(b, "id");
            const std::string* name = attr(b, "name");
            if (id) out.push_back({ *id, name ? *name : *id });
        }
    return out;
}

std::string JPSlotBanks::addBank() {
    const std::string id = JPOpenPnpIds::create("BANK-");
    JPXmlNode bank("bank");
    bank.attr("id", id).attr("name", id);
    bank.add(JPXmlNode("feeders"));
    if (JPXmlNode* banks = banksNode()) banks->add(std::move(bank));
    return id;
}

bool JPSlotBanks::removeBank(const std::string& bankId, std::string& why) {
    JPXmlNode* banks = banksNode();
    if (!banks || banks->children.size() < 2) {
        why = "Can't delete the only bank. There must always be one bank defined.";
        return false;
    }
    std::erase_if(banks->children, [&bankId](const JPXmlNode& b) {
        const std::string* id = b.get("id");
        return id && *id == bankId;
    });
    return true;
}

void JPSlotBanks::setBankName(const std::string& bankId, const std::string& name) {
    if (JPXmlNode* b = bankNode(bankId)) b->set("name", name);
}

std::vector<JPSlotBanks::Feeder> JPSlotBanks::feeders(const std::string& bankId) const {
    std::vector<Feeder> out;
    const JPXmlNode* bank = bankNode(bankId);
    const JPXmlNode* feeders = bank ? bank->child("feeders") : nullptr;
    if (!feeders) return out;
    for (const JPXmlNode& f : feeders->children) {
        const std::string* id = attr(f, "id");
        if (!id) continue;
        Feeder out1;
        out1.id = *id;
        out1.name = attr(f, "name") ? *attr(f, "name") : *id;
        out1.partId = attr(f, "part-id") ? *attr(f, "part-id") : std::string();
        if (const JPXmlNode* o = f.child("offsets")) out1.offsets = JPLocationXml::from(element(*o));
        out.push_back(std::move(out1));
    }
    return out;
}

std::optional<JPSlotBanks::Feeder> JPSlotBanks::feeder(const std::string& bankId, const std::string& feederId) const {
    for (const Feeder& f : feeders(bankId))
        if (f.id == feederId) return f;
    return std::nullopt;
}

std::string JPSlotBanks::addFeeder(const std::string& bankId, const std::string& name) {
    JPXmlNode* bank = bankNode(bankId);
    if (!bank) return {};
    JPXmlNode* feeders = bank->child("feeders");
    if (!feeders) feeders = &bank->add(JPXmlNode("feeders"));
    const std::string id = JPOpenPnpIds::create("SLOTFDR-");
    JPXmlNode f("feeder");
    f.attr("id", id).attr("name", name.empty() ? id : name);
    f.add(JPLocationXml::to("offsets", JPLocation(JPLengthUnit::Millimeters)));
    feeders->add(std::move(f));
    return id;
}

void JPSlotBanks::removeFeeder(const std::string& bankId, const std::string& feederId) {
    JPXmlNode* bank = bankNode(bankId);
    JPXmlNode* feeders = bank ? bank->child("feeders") : nullptr;
    if (!feeders) return;
    std::erase_if(feeders->children, [&feederId](const JPXmlNode& f) {
        const std::string* id = f.get("id");
        return id && *id == feederId;
    });
}

void JPSlotBanks::setFeederName(const std::string& bankId, const std::string& feederId, const std::string& name) {
    if (JPXmlNode* f = feederNode(bankId, feederId)) f->set("name", name);
}

void JPSlotBanks::setFeederPart(const std::string& bankId, const std::string& feederId, const std::string& partId) {
    JPXmlNode* f = feederNode(bankId, feederId);
    if (!f) return;
    if (!partId.empty()) {
        f->set("part-id", partId);
        return;
    }
    std::erase_if(f->attributes, [](const auto& a) { return a.first == "part-id"; });
}

void JPSlotBanks::setFeederOffsets(const std::string& bankId, const std::string& feederId, const JPLocation& offsets) {
    JPXmlNode* f = feederNode(bankId, feederId);
    if (!f) return;
    if (JPXmlNode* o = f->child("offsets")) *o = JPLocationXml::to("offsets", offsets);
    else f->add(JPLocationXml::to("offsets", offsets));
}

} // inline namespace jf
