// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPJobMachine.h"
#include "JPPhotonPacket.h"

#include <optional>
#include <string>

inline namespace jf {

// The Photon feeders' bus, as OpenPnP's PhotonBus: a packet from address 0,
// numbered in turn (0 to 255, then again), sent as the read parameter of the
// machine's PhotonFeederData actuator (its controller's "M485 {value}"); the
// reply is the packet read back, taken only when its number is the one sent.
class JPPhotonBus {
public:
    static constexpr const char* kDataActuator = "PhotonFeederData";

    explicit JPPhotonBus(JPJobMachine& machine) : m_machine(machine) {}
    // None when nothing (or nothing that is its reply) came back; `why` then
    // says why, when the actuator could not be read at all.
    std::optional<JPPhotonPacket> send(JPPhotonPacket packet, std::string& why);

private:
    JPJobMachine& m_machine;
};

} // inline namespace jf
