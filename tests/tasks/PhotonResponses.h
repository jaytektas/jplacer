// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

// OpenPnP's ResponsesHelper (its Photon tests'): the replies feeders give, to `toAddress`, packet id 0.

#include "tasks/JPPhotonCommands.h"

#include <string>

namespace photon_test {

using jf::JPPhotonCommands;
using jf::JPPhotonPacket;

class Responses {
public:
    explicit Responses(int toAddress) : m_to(toAddress) {}

    // OpenPnP's PacketBuilder.response.
    JPPhotonPacket response(int feederAddress) const {
        JPPhotonPacket p;
        p.toAddress = m_to;
        p.fromAddress = feederAddress;
        return p;
    }
    JPPhotonPacket error(JPPhotonCommands::Error e, int feederAddress) const { return std::move(response(feederAddress).putByte(int(e))); }
    JPPhotonPacket ok(int feederAddress) const { return error(JPPhotonCommands::Error::None, feederAddress); }
    JPPhotonPacket okUint16(int feederAddress, int value) const {
        return std::move(ok(feederAddress).putByte(value >> 8).putByte(value));
    }

    // errors
    JPPhotonPacket wrongFeederUUID(int feederAddress, const std::string& uuid) const {
        return std::move(error(JPPhotonCommands::Error::WrongFeederUuid, feederAddress).putUuid(uuid));
    }
    JPPhotonPacket couldNotReach(int feederAddress) const { return error(JPPhotonCommands::Error::CouldNotReach, feederAddress); }
    JPPhotonPacket uninitializedFeeder(int feederAddress, const std::string& uuid) const {
        return std::move(error(JPPhotonCommands::Error::UninitializedFeeder, feederAddress).putUuid(uuid));
    }
    static std::string timeout() { return "TIMEOUT"; }

    JPPhotonPacket getFeederIdOk(int feederAddress, const std::string& uuid) const { return std::move(ok(feederAddress).putUuid(uuid)); }
    JPPhotonPacket initializeFeederOk(int feederAddress, const std::string& uuid) const {
        return std::move(ok(feederAddress).putUuid(uuid));
    }
    JPPhotonPacket getVersionOk(int feederAddress, int version) const { return std::move(ok(feederAddress).putByte(version)); }
    JPPhotonPacket moveFeedForwardOk(int feederAddress, int expectedTimeToFeed) const { return okUint16(feederAddress, expectedTimeToFeed); }
    JPPhotonPacket moveFeedBackwardOk(int feederAddress, int expectedTimeToFeed) const { return okUint16(feederAddress, expectedTimeToFeed); }
    JPPhotonPacket moveFeedStatusOk(int feederAddress) const { return ok(feederAddress); }
    JPPhotonPacket getFeederAddressOk(int feederAddress) const { return ok(feederAddress); }
    JPPhotonPacket identifyFeederOk(int feederAddress) const { return ok(feederAddress); }
    JPPhotonPacket programFeederFloorOk(int feederAddress) const { return ok(feederAddress); }

private:
    int m_to;
};

} // namespace photon_test
