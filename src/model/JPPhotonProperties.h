// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPLocation.h"

#include "openpnp/JPXmlElement.h"
#include "openpnp/JPXmlNode.h"

#include <map>
#include <optional>
#include <vector>

inline namespace jf {

// What OpenPnP keeps for its Photon feeders on the machine (PhotonProperties):
// how many times a command is tried again ("PhotonFeeder.FeederCommunicationMaxRetry",
// 3), the highest feeder address a search asks ("PhotonFeeder.MaxFeederAddress",
// 50), and the feeder slots: each slot address's location, where set
// ("PhotonFeeder.FeederSlots"). Each is a machine property, an <entry> of
// machine.xml's <properties>.
class JPPhotonProperties {
public:
    static constexpr int kDefaultMaxRetry = 3;
    static constexpr int kDefaultMaxFeederAddress = 50;

    // Taken from an <entry>, when it is one of these; false otherwise.
    bool take(const JPXmlElement& entry);
    // The entries, as OpenPnP writes them.
    std::vector<JPXmlNode> toXml() const;

    int  feederCommunicationMaxRetry() const { return m_maxRetry; }
    void setFeederCommunicationMaxRetry(int n) { m_maxRetry = n; }
    int  maxFeederAddress() const { return m_maxFeederAddress; }
    void setMaxFeederAddress(int n) { m_maxFeederAddress = n; }
    std::optional<JPLocation> slotLocation(int address) const;
    void setSlotLocation(int address, const JPLocation& location);

private:
    int                        m_maxRetry = kDefaultMaxRetry;
    int                        m_maxFeederAddress = kDefaultMaxFeederAddress;
    std::map<int, JPLocation>  m_slots;
};

} // inline namespace jf
