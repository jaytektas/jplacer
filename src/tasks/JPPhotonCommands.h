// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPPhotonBusInterface.h"

#include <optional>
#include <string>

inline namespace jf {

// The Photon feeders' commands (OpenPnP's photon.protocol.commands): each one's packet, and its reply read as that
// command's Response reads it (not valid when of another length than it has).
class JPPhotonCommands {
public:
    // OpenPnP's ErrorTypes.
    enum class Error { None = 0x00, WrongFeederUuid = 0x01, CouldNotReach = 0x02, UninitializedFeeder = 0x03,
                       FeedingInProgress = 0x04, Unknown = 0xFF };
    // The commands' ids (each its packet's first byte).
    enum Id { GetFeederIdId = 0x01, InitializeFeederId = 0x02, GetVersionId = 0x03, MoveFeedForwardId = 0x04,
              MoveFeedBackwardId = 0x05, MoveFeedStatusId = 0x06, GetFeederAddressId = 0xC0, IdentifyFeederId = 0xC1,
              ProgramFeederFloorAddressId = 0xC2, UninitializedFeedersRespondId = 0xC3 };
    static constexpr int kBroadcast = 0xFF;

    struct Response {
        bool                 valid = false;
        int                  toAddress = 0, fromAddress = 0;
        std::optional<Error> error;               // none when not valid
        std::optional<std::string> uuid;          // a feeder's hardware id, where the reply has one
        int                  expectedTimeToFeed = 0;   // ms, a move's
    };

    // To a slot address. `distance` in tenths of a millimetre.
    static JPPhotonPacket getFeederId(int address);
    static JPPhotonPacket initializeFeeder(int address, const std::string& uuid);
    static JPPhotonPacket getVersion(int address);
    static JPPhotonPacket moveFeedForward(int address, int distance);
    static JPPhotonPacket moveFeedBackward(int address, int distance);
    static JPPhotonPacket moveFeedStatus(int address);
    // To every feeder (broadcast), by its hardware id.
    static JPPhotonPacket getFeederAddress(const std::string& uuid);
    static JPPhotonPacket identifyFeeder(const std::string& uuid);
    static JPPhotonPacket programFeederFloorAddress(const std::string& uuid, int floorAddress);
    static JPPhotonPacket uninitializedFeedersRespond();

    // The reply to command `id`.
    static Response decode(Id id, const JPPhotonPacket& reply);
    // OpenPnP's Command.send: `command` sent and its reply read; none when there was none.
    static std::optional<Response> send(JPPhotonBusInterface& bus, const JPPhotonPacket& command, std::string& why);

    static Error errorOf(int id);
};

} // inline namespace jf
