// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPPhotonBusInterface.h"

#include <functional>
#include <optional>
#include <string>

inline namespace jf {

// The Photon feeders' bus, as OpenPnP's PhotonBus: a packet from its address, numbered in turn (0 to 255, then
// again), sent as the read parameter of the machine's PhotonFeederData actuator (its controller's "M485 {value}");
// the reply is the packet read back, taken only when its number is the one sent.
class JPPhotonBus : public JPPhotonBusInterface {
public:
    static constexpr const char* kDataActuator = "PhotonFeederData";
    // The data actuator read with `parameter`: its value, else false and why.
    using Read = std::function<bool(const std::string& parameter, std::string& value, std::string& why)>;

    JPPhotonBus(int fromAddress, Read read) : m_fromAddress(fromAddress), m_read(std::move(read)) {}

    std::optional<JPPhotonPacket> send(JPPhotonPacket packet, std::string& why) override;

private:
    int  nextPacketId();

    int  m_fromAddress;
    Read m_read;
    int  m_packetId = 0;
};

} // inline namespace jf
