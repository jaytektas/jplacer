// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPNeoden4Protocol.h"

#include "common/JPlacerLog.h"

#include <j/core/Log.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <thread>

inline namespace jf {

namespace {

// OpenPnP's pauses after a failed exchange, before and after flushing the input.
constexpr int kRecoverMs = 1000;

// The CRC-16/CCITT table (polynomial 0x1021), as OpenPnP's checksumLookupTable.
constexpr std::array<uint16_t, 256> crcTable() {
    std::array<uint16_t, 256> t {};
    for (int n = 0; n < 256; ++n) {
        uint16_t c = uint16_t(n << 8);
        for (int k = 0; k < 8; ++k) c = (c & 0x8000) ? uint16_t((c << 1) ^ 0x1021) : uint16_t(c << 1);
        t[size_t(n)] = c;
    }
    return t;
}
constexpr std::array<uint16_t, 256> kCrc = crcTable();

void putInt32(int32_t v, std::array<uint8_t, 8>& b, size_t at) {
    for (size_t i = 0; i < 4; ++i) b[at + i] = uint8_t((uint32_t(v) >> (8 * i)) & 0xff);
}
void putInt16(int v, std::array<uint8_t, 8>& b, size_t at) {
    b[at] = uint8_t(uint32_t(v) & 0xff);
    b[at + 1] = uint8_t((uint32_t(v) >> 8) & 0xff);
}
void sleepMs(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

} // namespace

JPNeoden4Protocol::JPNeoden4Protocol(Port& port, int timeoutMs) : m_port(port), m_timeoutMs(timeoutMs) {}

uint8_t JPNeoden4Protocol::checksum(const uint8_t* bytes, size_t n) {
    uint16_t result = 0;
    for (size_t i = 0; i < n; ++i) result = uint16_t(kCrc[(bytes[i] ^ (result >> 8)) & 0xff] ^ uint16_t(result << 8));
    return uint8_t(result & 0xff);
}

uint8_t JPNeoden4Protocol::readByte() {
    const auto b = m_port.read(m_timeoutMs);
    if (!b) throw Failure { "no answer from the machine" };
    return *b;
}

void JPNeoden4Protocol::writeByte(uint8_t b) {
    if (!m_port.write(b)) throw Failure { "could not write to the machine" };
}

void JPNeoden4Protocol::expect(uint8_t expected) {
    const uint8_t got = readByte();
    if (got != expected) {
        char text[64];
        std::snprintf(text, sizeof text, "Expected %02x but received %02x.", expected, got);
        throw Failure { text };
    }
}

void JPNeoden4Protocol::pollFor(uint8_t command, uint8_t response, int timeoutMs) {
    const auto start = std::chrono::steady_clock::now();
    do {
        writeByte(command);
        if (timeoutMs > 0 && std::chrono::steady_clock::now() - start > std::chrono::milliseconds(timeoutMs))
            throw Failure { "Pool for timeout" };
        if (command == response) throw Failure { "Pool for error" };
    } while (readByte() != response);
}

void JPNeoden4Protocol::writePayload(const std::array<uint8_t, 8>& b) {
    for (uint8_t x : b) writeByte(x);
    writeByte(checksum(b.data(), b.size()));
}

std::array<uint8_t, 8> JPNeoden4Protocol::readPayload() {
    std::array<uint8_t, 8> b {};
    for (uint8_t& x : b) x = readByte();
    readByte();   // its checksum (OpenPnP does not check it either)
    return b;
}

void JPNeoden4Protocol::exchange(uint8_t command, uint8_t answer, uint8_t announce, uint8_t announced,
                                 const std::array<uint8_t, 8>& payload, uint8_t poll, uint8_t done) {
    writeByte(command);
    expect(answer);
    writeByte(announce);
    expect(announced);
    writePayload(payload);
    pollFor(poll, done);
}

bool JPNeoden4Protocol::isReady() {
    pollFor(0x45, 0x09);
    pollFor(0x05, 0x14);
    writeByte(0x85);
    expect(0x1c);
    return readPayload()[0] == 0;
}

void JPNeoden4Protocol::waitReady() {
    for (int waited = 0;;) {
        sleepMs(kStatusSleepMs);
        waited += kStatusSleepMs;
        if (waited >= kStatusMostMs) throw Failure { "timeout while waiting for status==ready" };
        if (isReady()) return;
    }
}

template <class Step>
bool JPNeoden4Protocol::once(std::string& why, Step&& step) {
    try {
        step();
        return true;
    } catch (const Failure& f) {
        why = f.why;
        return false;
    }
}

template <class Step>
bool JPNeoden4Protocol::tried(const char* what, int tries, std::string& why, Step&& step) {
    for (int i = 0; i < tries; ++i) {
        if (once(why, step)) return true;
        JLOGC(JPlacerLog::kDriver, JLogLevel::Warn) << "NeoDen4: " << what << ": " << why << ", recovering";
        sleepMs(kRecoverMs);
        m_port.flushInput();
        sleepMs(kRecoverMs);
    }
    why = std::string(what) + " error (" + why + ")";
    return false;
}

bool JPNeoden4Protocol::home(std::string& why) {
    // Every nozzle up and turned back, and let go, then homed.
    return once(why, [&] {
        for (int n = 1; n <= 4; ++n) {
            std::string w;
            if (!moveZ(n, 0, w) || !moveC(n, 0, w)) throw Failure { w };
        }
        std::string w;
        if (!moveC(0, 0, w)) throw Failure { w };
        std::array<uint8_t, 8> b {};
        putInt32(0x01, b, 0);
        exchange(0x47, 0x0b, 0xc7, 0x03, b, 0x07, 0x43);
        try {
            waitReady();
        } catch (const Failure&) {
            throw Failure { "home timeout while waiting for status==ready" };
        }
    });
}

bool JPNeoden4Protocol::moveSteps(int32_t sx, int32_t sy, std::string& why) {
    return once(why, [&] {
        JLOGC(JPlacerLog::kDriver, JLogLevel::Debug) << "NeoDen4 moveStep " << sx << ", " << sy;
        std::array<uint8_t, 8> b {};
        putInt32(sx, b, 0);
        putInt32(sy, b, 4);
        exchange(0x48, 0x05, 0xc8, 0x0d, b, 0x08, 0x4d);
        try {
            waitReady();
        } catch (const Failure&) {
            throw Failure { "moveXy timeout while waiting for status==ready" };
        }
    });
}

bool JPNeoden4Protocol::moveZ(int nozzle, double mm, std::string& why) {
    // The NeoDen's 0 is out (down), 13 retracted; OpenPnP's 0 up, -13 down: its depth in microns.
    return once(why, [&] {
        std::array<uint8_t, 8> b {};
        putInt16(int(std::abs(mm) * 1000.), b, 0);
        b[2] = 0x64;
        b[3] = uint8_t(nozzle);
        exchange(0x42, 0x0e, 0xc2, 0x06, b, 0x02, 0x46);
    });
}

bool JPNeoden4Protocol::moveC(int nozzle, double degrees, std::string& why) {
    return once(why, [&] {
        std::array<uint8_t, 8> b {};
        putInt16(int(-degrees * 10.), b, 0);
        b[2] = 0x32;
        b[3] = uint8_t(nozzle);
        exchange(0x41, 0x0d, 0xc1, 0x05, b, 0x01, 0x45);
    });
}

bool JPNeoden4Protocol::setMoveSpeed(double share, std::string& why) {
    return once(why, [&] {
        std::array<uint8_t, 8> b {};
        // A share of the fastest: 10 to 130.
        putInt16(int(120. * std::clamp(share, 0.0, 1.0) + 10), b, 0);
        b[2] = 0x09;
        b[4] = 0xc8;
        exchange(0x46, 0x0a, 0xc6, 0x02, b, 0x06, 0x42);
    });
}

bool JPNeoden4Protocol::feed(int id, int strength, int feedRate, std::string& why) {
    JLOGC(JPlacerLog::kDriver, JLogLevel::Debug) << "NeoDen4 feed, id=" << id << ", strength=" << strength << ", feedRate=" << feedRate;
    return tried("Feed", kTries, why, [&] {
        writeByte(0x3f);
        expect(0x0c);
        writeByte(uint8_t(0x46 + id));
        readByte();
        writeByte(0xff);
        expect(0x00);
        writeByte(uint8_t(0x46 + id));
        readByte();
        std::array<uint8_t, 8> b {};
        b[0] = uint8_t(strength);
        b[1] = uint8_t(feedRate);
        writePayload(b);
        writeByte(0x3f);
        expect(0x0c);
        writeByte(uint8_t(0x46 + id));
        readByte();
    });
}

bool JPNeoden4Protocol::changeFeederId(int oldId, int newId, std::string& why) {
    if (oldId < 0 || oldId >= 100) {
        why = "changeFeederId oldId must be between 0-99.";
        return false;
    }
    if (newId < 0 || newId >= 100) {
        why = "changeFeederId newId must be between 0-99.";
        return false;
    }
    return tried("changeFeederId", kTries, why, [&] {
        writeByte(0x3f);
        expect(0x0c);
        writeByte(uint8_t(0x46 + oldId));
        readByte();
        writeByte(0xff);
        expect(0x00);
        writeByte(uint8_t(0x46 + oldId));
        readByte();
        std::array<uint8_t, 8> b {};
        b[0] = uint8_t(newId);
        b[7] = 0x01;
        writePayload(b);
        writeByte(0x3f);
        expect(0x0c);
        writeByte(uint8_t(0x46 + oldId));
        readByte();
    });
}

bool JPNeoden4Protocol::peel(int id, int strength, int feedRate, std::string& why) {
    JLOGC(JPlacerLog::kDriver, JLogLevel::Debug) << "NeoDen4 peel, id=" << id << ", strength=" << strength << ", feedRate=" << feedRate;
    return tried("Peel", kTries, why, [&] {
        // Peelers 20 and up are the top half's, counted from 1 there.
        const bool top = id >= 20;
        std::array<uint8_t, 8> b {};
        b[0] = uint8_t(top ? id - 19 : id);
        b[1] = uint8_t(feedRate);
        b[2] = uint8_t(strength);
        if (top) exchange(0x4e, 0x03, 0xce, 0x0b, b, 0x0e, 0x4b);
        else exchange(0x4c, 0x01, 0xcc, 0x09, b, 0x0c, 0x49);
    });
}

bool JPNeoden4Protocol::setAir(int nozzle, int value, std::string& why) {
    return tried("Actuate", kTries, why, [&] {
        std::array<uint8_t, 8> b {};
        b[0] = uint8_t(int8_t(value));
        b[1] = uint8_t(nozzle);
        exchange(0x43, 0x0f, 0xc3, 0x07, b, 0x03, 0x47);
    });
}

bool JPNeoden4Protocol::lightsDown(int level, std::string& why) {
    return tried("Actuate", kTries, why, [&] {
        std::array<uint8_t, 8> b {};
        b[0] = uint8_t(level);
        exchange(0x44, 0x08, 0xc4, 0x00, b, 0x04, 0x40);
    });
}

bool JPNeoden4Protocol::lightsUp(int level, std::string& why) {
    return tried("Actuate", kTries, why, [&] {
        std::array<uint8_t, 8> b {};
        b[4] = uint8_t(level);
        exchange(0x47, 0x0b, 0xc7, 0x03, b, 0x07, 0x43);
    });
}

void JPNeoden4Protocol::stopRail() {
    std::array<uint8_t, 8> b {};
    b[1] = 0x02;
    exchange(0x47, 0x0b, 0xc7, 0x03, b, 0x07, 0x43);
}

void JPNeoden4Protocol::railSpeed(int speed) {
    // As OpenPnP: its size, kept to 20 .. 200.
    speed = std::clamp(std::abs(speed), 20, 200);
    std::array<uint8_t, 8> b {};
    b[0] = 0x32;
    b[1] = 0x09;
    b[3] = uint8_t(speed);
    exchange(0x46, 0x0a, 0xc6, 0x02, b, 0x06, 0x42);
}

void JPNeoden4Protocol::runRail(bool forward) {
    std::array<uint8_t, 8> b {};
    if (forward) {
        b[4] = 0xc9;
        b[5] = 0x03;
        b[6] = 0x0c;
    } else {
        b[4] = 0x37;
        b[5] = 0xfc;
        b[6] = 0xf3;
        b[7] = 0xff;
    }
    exchange(0x49, 0x04, 0xc9, 0x0c, b, 0x09, 0x4c);
}

bool JPNeoden4Protocol::rails(int speed, std::string& why) {
    return tried("Actuate", kTries, why, [&] {
        stopRail();
        if (speed == 0) return;
        railSpeed(speed);
        runRail(speed > 0);
    });
}

bool JPNeoden4Protocol::buzzer(bool on, std::string& why) {
    return once(why, [&] {
        std::array<uint8_t, 8> b {};
        b[5] = on ? 0x01 : 0x00;
        exchange(0x47, 0x0b, 0xc7, 0x03, b, 0x07, 0x43);
    });
}

bool JPNeoden4Protocol::readAir(int nozzle, int& value, std::string& why) {
    std::array<uint8_t, 8> payload {};
    if (!tried("getNozzleAirValue", kReadTries, why, [&] {
            writeByte(0x40);
            expect(0x0c);
            writeByte(0x00);
            expect(0x11);
            writeByte(0x80);
            expect(0x19);
            payload = readPayload();
        }))
        return false;
    value = int(int8_t(payload[size_t(std::clamp(nozzle, 1, 4) - 1)]));
    // OpenPnP's: a small tip can read below -128, which wraps round past 110: taken back below -128.
    if (value > 110) value = -128 - (128 - value);
    return true;
}

bool JPNeoden4Protocol::ready(bool& isReady_, std::string& why) {
    return once(why, [&] { isReady_ = isReady(); });
}

} // inline namespace jf
