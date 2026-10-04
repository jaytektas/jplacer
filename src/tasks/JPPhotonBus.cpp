// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPPhotonBus.h"

#include <atomic>

inline namespace jf {

namespace {

// OpenPnP's bus is one for the machine: its packets are numbered in turn.
std::atomic<int> s_packetId { 0 };

} // namespace

std::optional<JPPhotonPacket> JPPhotonBus::send(JPPhotonPacket packet, std::string& why) {
    packet.fromAddress = 0;
    packet.packetId = s_packetId.fetch_add(1) % 256;
    std::string reply;
    if (!m_machine.readActuator(kDataActuator, packet.toByteString(), reply, why)) return std::nullopt;
    auto got = JPPhotonPacket::decode(reply);
    if (!got || got->packetId != packet.packetId) return std::nullopt;
    return got;
}

} // inline namespace jf
