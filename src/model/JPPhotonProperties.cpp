// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPPhotonProperties.h"

#include "JPLocationXml.h"

#include <cstdlib>
#include <functional>

inline namespace jf {

namespace {

constexpr const char* kMaxRetry = "PhotonFeeder.FeederCommunicationMaxRetry";
constexpr const char* kSlots = "PhotonFeeder.FeederSlots";
constexpr const char* kMaxAddress = "PhotonFeeder.MaxFeederAddress";

JPXmlNode entry(const std::string& key, JPXmlNode value) {
    JPXmlNode e("entry");
    JPXmlNode name("string");
    name.text = key;
    e.add(std::move(name));
    e.add(std::move(value));
    return e;
}

JPXmlNode integer(int v) {
    JPXmlNode n("object");
    n.attr("class", "java.lang.Integer");
    n.text = std::to_string(v);
    return n;
}

} // namespace

bool JPPhotonProperties::take(const JPXmlElement& e) {
    const JPXmlElement* name = e.child("string");
    if (!name) return false;
    // The value: the entry's other element.
    const JPXmlElement* value = nullptr;
    for (const JPXmlElement& c : e.children)
        if (&c != name) value = &c;
    if (!value) return false;
    if (name->text == kMaxRetry) {
        m_maxRetry = std::atoi(value->text.c_str());
        return true;
    }
    if (name->text == kMaxAddress) {
        m_maxFeederAddress = std::atoi(value->text.c_str());
        return true;
    }
    if (name->text != kSlots) return false;
    // Each <slot address="n"> with its <location>, wherever the list puts them.
    std::function<void(const JPXmlElement&)> walk = [&](const JPXmlElement& n) {
        for (const JPXmlElement& c : n.children) {
            if (c.name == "slot" && !c.attr("address").empty()) {
                if (const JPXmlElement* l = c.child("location"))
                    m_slots[std::atoi(c.attr("address").c_str())] = JPLocationXml::from(*l);
                continue;
            }
            walk(c);
        }
    };
    walk(*value);
    return true;
}

std::vector<JPXmlNode> JPPhotonProperties::toXml() const {
    JPXmlNode slots("object");
    slots.attr("class", "org.openpnp.machine.photon.PhotonFeederSlots");
    JPXmlNode& list = slots.add(JPXmlNode("slots"));
    for (const auto& [address, location] : m_slots) {
        JPXmlNode slot("slot");
        slot.attr("address", std::to_string(address));
        slot.add(JPLocationXml::to("location", location));
        list.add(std::move(slot));
    }
    return { entry(kMaxRetry, integer(m_maxRetry)), entry(kSlots, std::move(slots)), entry(kMaxAddress, integer(m_maxFeederAddress)) };
}

std::optional<JPLocation> JPPhotonProperties::slotLocation(int address) const {
    const auto i = m_slots.find(address);
    return i == m_slots.end() ? std::nullopt : std::optional(i->second);
}

void JPPhotonProperties::setSlotLocation(int address, const JPLocation& location) { m_slots[address] = location; }

} // inline namespace jf
