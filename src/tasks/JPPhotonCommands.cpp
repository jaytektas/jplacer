// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPPhotonCommands.h"

inline namespace jf {

namespace {

using Response = JPPhotonCommands::Response;
using Error = JPPhotonCommands::Error;

// A reply of an error byte and, without other, nothing more.
Response errorOnly(const JPPhotonPacket& p) {
    Response r { false, p.toAddress, p.fromAddress, std::nullopt, std::nullopt, 0 };
    if (p.payload.size() != 1) return r;
    r.valid = true;
    r.error = JPPhotonCommands::errorOf(p.payload[0]);
    return r;
}

// An error byte and a hardware id.
Response errorUuid(const JPPhotonPacket& p) {
    Response r { false, p.toAddress, p.fromAddress, std::nullopt, std::nullopt, 0 };
    if (p.payload.size() != 13) return r;
    r.valid = true;
    r.error = JPPhotonCommands::errorOf(p.payload[0]);
    r.uuid = p.uuid(1);
    return r;
}

} // namespace

Error JPPhotonCommands::errorOf(int id) {
    switch (id) {
        case 0x00: return Error::None;
        case 0x01: return Error::WrongFeederUuid;
        case 0x02: return Error::CouldNotReach;
        case 0x03: return Error::UninitializedFeeder;
        case 0x04: return Error::FeedingInProgress;
        default:   return Error::Unknown;
    }
}

JPPhotonPacket JPPhotonCommands::getFeederId(int address) { return JPPhotonPacket::command(GetFeederIdId, address); }

JPPhotonPacket JPPhotonCommands::initializeFeeder(int address, const std::string& uuid) {
    return std::move(JPPhotonPacket::command(InitializeFeederId, address).putUuid(uuid));
}

JPPhotonPacket JPPhotonCommands::getVersion(int address) { return JPPhotonPacket::command(GetVersionId, address); }

JPPhotonPacket JPPhotonCommands::moveFeedForward(int address, int distance) {
    return std::move(JPPhotonPacket::command(MoveFeedForwardId, address).putByte(distance));
}

JPPhotonPacket JPPhotonCommands::moveFeedBackward(int address, int distance) {
    return std::move(JPPhotonPacket::command(MoveFeedBackwardId, address).putByte(distance));
}

JPPhotonPacket JPPhotonCommands::moveFeedStatus(int address) { return JPPhotonPacket::command(MoveFeedStatusId, address); }

JPPhotonPacket JPPhotonCommands::getFeederAddress(const std::string& uuid) {
    return std::move(JPPhotonPacket::command(GetFeederAddressId, kBroadcast).putUuid(uuid));
}

JPPhotonPacket JPPhotonCommands::identifyFeeder(const std::string& uuid) {
    return std::move(JPPhotonPacket::command(IdentifyFeederId, kBroadcast).putUuid(uuid));
}

JPPhotonPacket JPPhotonCommands::programFeederFloorAddress(const std::string& uuid, int floorAddress) {
    return std::move(JPPhotonPacket::command(ProgramFeederFloorAddressId, kBroadcast).putUuid(uuid).putByte(floorAddress));
}

JPPhotonPacket JPPhotonCommands::uninitializedFeedersRespond() {
    return JPPhotonPacket::command(UninitializedFeedersRespondId, kBroadcast);
}

Response JPPhotonCommands::decode(Id id, const JPPhotonPacket& p) {
    switch (id) {
        case GetFeederIdId:
        case InitializeFeederId:
        case UninitializedFeedersRespondId:
            return errorUuid(p);
        case MoveFeedForwardId: {
            // The error byte at least; the time to feed after it when there is no error.
            Response r { false, p.toAddress, p.fromAddress, std::nullopt, std::nullopt, 0 };
            if (p.payload.empty()) return r;
            r.error = errorOf(p.payload[0]);
            if (r.error == Error::None) r.expectedTimeToFeed = p.uint16(1);
            r.valid = true;
            return r;
        }
        case MoveFeedBackwardId: {
            Response r { false, p.toAddress, p.fromAddress, std::nullopt, std::nullopt, 0 };
            if (p.payload.size() != 3) return r;
            r.valid = true;
            r.error = errorOf(p.payload[0]);
            r.expectedTimeToFeed = p.uint16(1);
            return r;
        }
        case GetVersionId:
        case MoveFeedStatusId:
        case GetFeederAddressId:
        case IdentifyFeederId:
        case ProgramFeederFloorAddressId:
            return errorOnly(p);
    }
    return errorOnly(p);
}

std::optional<Response> JPPhotonCommands::send(JPPhotonBusInterface& bus, const JPPhotonPacket& command, std::string& why) {
    const auto got = bus.send(command, why);
    if (!got) return std::nullopt;
    return decode(Id(command.payload.at(0)), *got);
}

} // inline namespace jf
