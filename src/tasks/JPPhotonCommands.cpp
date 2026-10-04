// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPPhotonCommands.h"

#include <cstdlib>

inline namespace jf {

namespace {

constexpr int kBroadcast = 0xFF;
constexpr size_t kUuidBytes = 12;

enum Id { GetFeederId = 0x01, InitializeFeeder = 0x02, GetVersion = 0x03, MoveFeedForward = 0x04, MoveFeedBackward = 0x05,
          MoveFeedStatus = 0x06, GetFeederAddress = 0xC0, IdentifyFeeder = 0xC1, ProgramFeederFloorAddress = 0xC2,
          UninitializedFeedersRespond = 0xC3 };

using Response = JPPhotonCommands::Response;
using Error = JPPhotonCommands::Error;

JPPhotonPacket command(int id, int to) {
    JPPhotonPacket p;
    p.toAddress = to;
    p.payload.push_back(uint8_t(id));
    return p;
}

void putUuid(JPPhotonPacket& p, const std::string& uuid) {
    for (size_t i = 0; i < kUuidBytes; ++i) {
        const std::string pair = 2 * i + 1 < uuid.size() ? uuid.substr(2 * i, 2) : "00";
        p.payload.push_back(uint8_t(std::strtol(pair.c_str(), nullptr, 16)));
    }
}

Error errorOf(int id) {
    switch (id) {
        case 0x00: return Error::None;
        case 0x01: return Error::WrongFeederUuid;
        case 0x02: return Error::CouldNotReach;
        case 0x03: return Error::UninitializedFeeder;
        case 0x04: return Error::FeedingInProgress;
        default:   return Error::Unknown;
    }
}

// The reply as OpenPnP reads each kind: an error byte alone; an error and a
// hardware id; an error and, without error, the time a move takes.
enum class Shape { Error, ErrorUuid, Move, MoveExact };

std::optional<Response> send(JPPhotonBus& bus, JPPhotonPacket packet, Shape shape, std::string& why) {
    const auto got = bus.send(std::move(packet), why);
    if (!got) return std::nullopt;
    Response r;
    r.fromAddress = got->fromAddress;
    const size_t n = got->payload.size();
    switch (shape) {
        case Shape::Error:
            r.valid = n == 1;
            break;
        case Shape::ErrorUuid:
            r.valid = n == 1 + kUuidBytes;
            if (r.valid) r.uuid = got->uuid(1);
            break;
        case Shape::Move:
            r.valid = n >= 1;
            break;
        case Shape::MoveExact:
            r.valid = n == 3;
            break;
    }
    if (!r.valid) return r;
    r.error = errorOf(got->payload[0]);
    if ((shape == Shape::Move && r.error == Error::None) || shape == Shape::MoveExact) r.expectedTimeToFeed = got->uint16(1);
    return r;
}

} // namespace

std::optional<Response> JPPhotonCommands::getFeederId(JPPhotonBus& bus, int address, std::string& why) {
    return send(bus, command(GetFeederId, address), Shape::ErrorUuid, why);
}

std::optional<Response> JPPhotonCommands::initializeFeeder(JPPhotonBus& bus, int address, const std::string& uuid,
                                                           std::string& why) {
    JPPhotonPacket p = command(InitializeFeeder, address);
    putUuid(p, uuid);
    return send(bus, std::move(p), Shape::ErrorUuid, why);
}

std::optional<Response> JPPhotonCommands::getVersion(JPPhotonBus& bus, int address, std::string& why) {
    return send(bus, command(GetVersion, address), Shape::Error, why);
}

std::optional<Response> JPPhotonCommands::moveFeedForward(JPPhotonBus& bus, int address, int distance, std::string& why) {
    JPPhotonPacket p = command(MoveFeedForward, address);
    p.payload.push_back(uint8_t(distance & 0xFF));
    return send(bus, std::move(p), Shape::Move, why);
}

std::optional<Response> JPPhotonCommands::moveFeedBackward(JPPhotonBus& bus, int address, int distance, std::string& why) {
    JPPhotonPacket p = command(MoveFeedBackward, address);
    p.payload.push_back(uint8_t(distance & 0xFF));
    return send(bus, std::move(p), Shape::MoveExact, why);
}

std::optional<Response> JPPhotonCommands::moveFeedStatus(JPPhotonBus& bus, int address, std::string& why) {
    return send(bus, command(MoveFeedStatus, address), Shape::Error, why);
}

std::optional<Response> JPPhotonCommands::getFeederAddress(JPPhotonBus& bus, const std::string& uuid, std::string& why) {
    JPPhotonPacket p = command(GetFeederAddress, kBroadcast);
    putUuid(p, uuid);
    return send(bus, std::move(p), Shape::Error, why);
}

std::optional<Response> JPPhotonCommands::identifyFeeder(JPPhotonBus& bus, const std::string& uuid, std::string& why) {
    JPPhotonPacket p = command(IdentifyFeeder, kBroadcast);
    putUuid(p, uuid);
    return send(bus, std::move(p), Shape::Error, why);
}

std::optional<Response> JPPhotonCommands::programFeederFloorAddress(JPPhotonBus& bus, const std::string& uuid, int floorAddress,
                                                                    std::string& why) {
    JPPhotonPacket p = command(ProgramFeederFloorAddress, kBroadcast);
    putUuid(p, uuid);
    p.payload.push_back(uint8_t(floorAddress & 0xFF));
    return send(bus, std::move(p), Shape::Error, why);
}

std::optional<Response> JPPhotonCommands::uninitializedFeedersRespond(JPPhotonBus& bus, std::string& why) {
    return send(bus, command(UninitializedFeedersRespond, kBroadcast), Shape::ErrorUuid, why);
}

} // inline namespace jf
