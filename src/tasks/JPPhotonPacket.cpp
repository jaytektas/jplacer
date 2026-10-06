// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPPhotonPacket.h"

#include <cctype>
#include <cstdio>
#include <utility>

inline namespace jf {

namespace {

// OpenPnP's CRC8_107: polynomial 0x107, no reflection, starting at 0.
struct Crc8 {
    int crc = 0;
    void add(int data) {
        crc ^= (data & 0xFF) << 8;
        for (int bit = 0; bit < 8; ++bit) {
            if (crc & 0x8000) crc ^= 0x1070 << 3;
            crc = (crc << 1) & 0xFFFF;
        }
    }
    int value() const { return (crc >> 8) & 0xFF; }
};

// A feeder's hardware id: 12 bytes.
constexpr size_t kUuidBytes = 12;

int hexDigit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    const char l = char(std::tolower(static_cast<unsigned char>(c)));
    if (l >= 'a' && l <= 'f') return l - 'a' + 10;
    return -1;
}

} // namespace

JPPhotonPacket JPPhotonPacket::command(int commandId, int toAddress) {
    JPPhotonPacket p;
    p.toAddress = toAddress;
    return std::move(p.putByte(commandId));
}

JPPhotonPacket& JPPhotonPacket::putByte(int data) {
    payload.push_back(uint8_t(data & 0xFF));
    return *this;
}

JPPhotonPacket& JPPhotonPacket::putUuid(const std::string& uuid) {
    for (size_t i = 0; i < kUuidBytes; ++i) putByte(2 * i + 1 < uuid.size() ? byteAt(uuid, i) : 0);
    return *this;
}

int JPPhotonPacket::byteAt(const std::string& text, size_t index) {
    const int hi = hexDigit(text[2 * index]), lo = hexDigit(text[2 * index + 1]);
    return (hi < 0 || lo < 0) ? -1 : hi * 16 + lo;
}

int JPPhotonPacket::crc() const {
    Crc8 c;
    c.add(toAddress);
    c.add(fromAddress);
    c.add(packetId);
    c.add(int(payload.size()));
    for (const uint8_t b : payload) c.add(b);
    return c.value();
}

std::string JPPhotonPacket::toByteString() const {
    std::string out;
    char buf[3];
    for (const int b : { toAddress, fromAddress, packetId, int(payload.size()), crc() }) {
        std::snprintf(buf, sizeof buf, "%02X", b & 0xFF);
        out += buf;
    }
    for (const uint8_t b : payload) {
        std::snprintf(buf, sizeof buf, "%02X", b);
        out += buf;
    }
    return out;
}

std::optional<JPPhotonPacket> JPPhotonPacket::decode(const std::string& text) {
    // At least a header: to, from, packet id, length, CRC.
    if (text == "TIMEOUT" || text.size() % 2 != 0 || text.size() < 10) return std::nullopt;
    std::vector<int> data(text.size() / 2);
    for (size_t i = 0; i < data.size(); ++i)
        if ((data[i] = byteAt(text, i)) < 0) return std::nullopt;
    JPPhotonPacket p;
    p.toAddress = data[0];
    p.fromAddress = data[1];
    p.packetId = data[2];
    if (data[3] != int(data.size()) - 5) return std::nullopt;
    for (size_t i = 5; i < data.size(); ++i) p.payload.push_back(uint8_t(data[i]));
    if (p.crc() != data[4]) return std::nullopt;
    return p;
}

std::string JPPhotonPacket::uuid(size_t from) const {
    std::string out;
    char buf[3];
    for (size_t i = 0; i < kUuidBytes && from + i < payload.size(); ++i) {
        std::snprintf(buf, sizeof buf, "%02X", payload[from + i]);
        out += buf;
    }
    return out;
}

int JPPhotonPacket::uint16(size_t from) const {
    return from + 1 < payload.size() ? 256 * payload[from] + payload[from + 1] : 0;
}

} // inline namespace jf
