// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPPhotonPacket.h"

#include <optional>
#include <string>

inline namespace jf {

// OpenPnP's PhotonBusInterface: what sends a packet to the Photon feeders and gives back their reply.
class JPPhotonBusInterface {
public:
    virtual ~JPPhotonBusInterface() = default;
    // None when nothing (or nothing that is its reply) came back; `why` then says why, when it could not be sent.
    virtual std::optional<JPPhotonPacket> send(JPPhotonPacket packet, std::string& why) = 0;
};

} // inline namespace jf
