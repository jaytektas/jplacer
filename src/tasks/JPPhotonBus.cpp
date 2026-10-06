// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPPhotonBus.h"

inline namespace jf {

int JPPhotonBus::nextPacketId() {
    const int current = m_packetId;
    m_packetId = (m_packetId + 1) % 256;
    return current;
}

std::optional<JPPhotonPacket> JPPhotonBus::send(JPPhotonPacket packet, std::string& why) {
    packet.fromAddress = m_fromAddress;
    packet.packetId = nextPacketId();
    std::string reply;
    if (!m_read(packet.toByteString(), reply, why)) return std::nullopt;
    auto got = JPPhotonPacket::decode(reply);
    // Is this our packet?
    if (!got || got->packetId != packet.packetId) return std::nullopt;
    return got;
}

} // inline namespace jf
