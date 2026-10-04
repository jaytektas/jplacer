// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPPhotonBus.h"

#include <optional>
#include <string>

inline namespace jf {

// The Photon feeders' commands (OpenPnP's photon.protocol.commands), each a
// packet sent on the bus and its reply read: none when there was no reply
// (or not its). A reply of an unexpected length is not valid.
class JPPhotonCommands {
public:
    // OpenPnP's ErrorTypes.
    enum class Error { None = 0x00, WrongFeederUuid = 0x01, CouldNotReach = 0x02, UninitializedFeeder = 0x03,
                       FeedingInProgress = 0x04, Unknown = 0xFF };
    struct Response {
        bool        valid = false;
        int         fromAddress = 0;
        Error       error = Error::Unknown;
        std::string uuid;                 // a feeder's hardware id, where the reply has one
        int         expectedTimeToFeed = 0;   // ms, a move's
    };
    // To a slot address.
    static std::optional<Response> getFeederId(JPPhotonBus& bus, int address, std::string& why);
    static std::optional<Response> initializeFeeder(JPPhotonBus& bus, int address, const std::string& uuid, std::string& why);
    static std::optional<Response> getVersion(JPPhotonBus& bus, int address, std::string& why);
    // `distance` in tenths of a millimetre.
    static std::optional<Response> moveFeedForward(JPPhotonBus& bus, int address, int distance, std::string& why);
    static std::optional<Response> moveFeedBackward(JPPhotonBus& bus, int address, int distance, std::string& why);
    static std::optional<Response> moveFeedStatus(JPPhotonBus& bus, int address, std::string& why);
    // To every feeder (broadcast), by its hardware id.
    static std::optional<Response> getFeederAddress(JPPhotonBus& bus, const std::string& uuid, std::string& why);
    static std::optional<Response> identifyFeeder(JPPhotonBus& bus, const std::string& uuid, std::string& why);
    static std::optional<Response> programFeederFloorAddress(JPPhotonBus& bus, const std::string& uuid, int floorAddress,
                                                             std::string& why);
    static std::optional<Response> uninitializedFeedersRespond(JPPhotonBus& bus, std::string& why);
};

} // inline namespace jf
