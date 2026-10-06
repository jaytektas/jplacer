// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

inline namespace jf {

// A packet of the Photon feeders' bus (OpenPnP's photon.protocol.Packet):
// to, from, packet id, payload length and a CRC-8 (polynomial 0x07, over the
// rest of the header and the payload), then the payload; written and read
// as two hexadecimal digits a byte.
struct JPPhotonPacket {
    int                  toAddress = 0, fromAddress = 0, packetId = 0;
    std::vector<uint8_t> payload;

    // OpenPnP's PacketBuilder.command: a command to an address, its id the payload's first byte.
    static JPPhotonPacket command(int commandId, int toAddress);
    // A byte, and a feeder's hardware id (24 hex digits: 12 bytes), added to the payload.
    JPPhotonPacket& putByte(int data);
    JPPhotonPacket& putUuid(const std::string& uuid);

    // The CRC of the header (its length the payload's) and payload.
    int         crc() const;
    std::string toByteString() const;
    // None when it is "TIMEOUT", not whole bytes, shorter than a header, of
    // another length than it says, or its CRC is wrong.
    static std::optional<JPPhotonPacket> decode(const std::string& text);

    // The payload read: 12 bytes as hex (a feeder's id), a big-endian 16 bits.
    std::string uuid(size_t from) const;
    int         uint16(size_t from) const;
    // The byte at `index` of a text of two hexadecimal digits a byte (OpenPnP's getByteAtPhoton).
    static int byteAt(const std::string& text, size_t index);
};

} // inline namespace jf
