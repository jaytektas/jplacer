// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPLocation.h"

#include "openpnp/JPXmlElement.h"
#include "openpnp/JPXmlNode.h"

#include <optional>
#include <string>
#include <vector>

inline namespace jf {

// The banks of feeders that slot feeders are loaded from (OpenPnP's
// ReferenceSlotAutoFeeder and SlotSchultzFeeder banks), kept as OpenPnP keeps
// them: one machine property each ("ReferenceAutoFeederSlot.banks",
// "SchultzFeederSlot.banks"), an <entry> of machine.xml's <properties>, its
// banks each a name and its feeders, each feeder a name, a part and its
// offsets from the slot's location. The entry is kept as read and changed in
// place, so it is written back as OpenPnP wrote it. There is always a bank.
class JPSlotBanks {
public:
    struct Bank {
        std::string id, name;
    };
    struct Feeder {
        std::string id, name, partId;
        JPLocation  offsets { JPLengthUnit::Millimeters };
    };

    // The property's name for a slot feeder kind ("ReferenceSlotAutoFeeder",
    // "SlotSchultzFeeder"); empty for any other kind.
    static std::string keyFor(const std::string& typeName);
    // A property of `key` with one bank, "Default" (as OpenPnP makes one).
    explicit JPSlotBanks(const std::string& key);
    // From machine.xml's <entry> (its <string> the key).
    static std::optional<JPSlotBanks> fromXml(const JPXmlElement& entry);
    const JPXmlNode& toXml() const { return m_entry; }
    std::string key() const;

    std::vector<Bank> banks() const;
    // A new bank (OpenPnP's "BANK-" id, named by it); its id.
    std::string addBank();
    // False, and why, for the only bank.
    bool removeBank(const std::string& bankId, std::string& why);
    void setBankName(const std::string& bankId, const std::string& name);

    std::vector<Feeder>   feeders(const std::string& bankId) const;
    std::optional<Feeder> feeder(const std::string& bankId, const std::string& feederId) const;
    // A new feeder in the bank (OpenPnP's "SLOTFDR-" id; named `name`, else by its id); its id.
    std::string addFeeder(const std::string& bankId, const std::string& name = {});
    void        removeFeeder(const std::string& bankId, const std::string& feederId);
    void        setFeederName(const std::string& bankId, const std::string& feederId, const std::string& name);
    void        setFeederPart(const std::string& bankId, const std::string& feederId, const std::string& partId);
    void        setFeederOffsets(const std::string& bankId, const std::string& feederId, const JPLocation& offsets);

private:
    JPXmlNode*       banksNode();
    const JPXmlNode* banksNode() const;
    JPXmlNode*       bankNode(const std::string& bankId);
    const JPXmlNode* bankNode(const std::string& bankId) const;
    JPXmlNode*       feederNode(const std::string& bankId, const std::string& feederId);

    JPXmlNode m_entry { "entry" };
};

} // inline namespace jf
