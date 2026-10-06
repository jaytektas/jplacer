// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// The NeoDen 4's binary protocol, against a stand-in machine that answers
// each exchange as the NeoDen does: the checksum (CRC-16/CCITT's low byte),
// homing, moves in steps, each nozzle's Z and rotation, the speed, a feeder
// and a peeler (top half too), the air set and read (a reading past 110
// taken as below -128), and a failure answered by trying again.
// Tests check with assert(); a Release build must not compile it away.
#undef NDEBUG
#include <cassert>

#include "machine/JPNeoden4Protocol.h"

#include <algorithm>
#include <deque>
#include <map>
#include <vector>

using namespace jf;

namespace {

// A NeoDen 4 as OpenPnP's driver sees it: each byte answered, each payload kept.
class FakeNeoden : public JPNeoden4Protocol::Port {
public:
    struct Payload {
        uint8_t announce;
        std::vector<uint8_t> bytes;
    };
    std::vector<Payload>   payloads;    // each announced payload, as received
    std::vector<uint8_t>   written;
    std::array<int8_t, 4>  air { -100, -20, 0, 120 };
    int                    refuse = 0;  // answers to spoil (a wrong byte), from the next
    int                    busyPolls = 0;

    bool write(uint8_t b) override {
        written.push_back(b);
        if (m_payload) {
            m_buffer.push_back(b);
            if (m_buffer.size() == 9) {
                assert(JPNeoden4Protocol::checksum(m_buffer.data(), 8) == m_buffer[8]);
                payloads.push_back({ m_announce, { m_buffer.begin(), m_buffer.begin() + 8 } });
                m_buffer.clear();
                m_payload = false;
            }
            return true;
        }
        // A feeder exchange, after 0x3f: its id, 0xff (answered 0x00), its id again and then its
        // payload unannounced, 0x3f again (0x0c) and its id once more.
        if (m_feeding > 0) {
            if (m_feeding == 2) assert(b == 0xff);
            if (m_feeding == 4) assert(b == 0x3f);
            answer(m_feeding == 4 ? 0x0c : 0x00);
            if (m_feeding == 3) {
                m_payload = true;
                m_announce = 0x3f;
            }
            m_feeding = m_feeding == 5 ? 0 : m_feeding + 1;
            return true;
        }
        static const std::map<uint8_t, uint8_t> kAnswers {
            { 0x47, 0x0b }, { 0xc7, 0x03 }, { 0x07, 0x43 }, { 0x48, 0x05 }, { 0xc8, 0x0d }, { 0x08, 0x4d },
            { 0x42, 0x0e }, { 0xc2, 0x06 }, { 0x02, 0x46 }, { 0x41, 0x0d }, { 0xc1, 0x05 }, { 0x01, 0x45 },
            { 0x46, 0x0a }, { 0xc6, 0x02 }, { 0x06, 0x42 }, { 0x43, 0x0f }, { 0xc3, 0x07 }, { 0x03, 0x47 },
            { 0x44, 0x08 }, { 0xc4, 0x00 }, { 0x04, 0x40 }, { 0x49, 0x04 }, { 0xc9, 0x0c }, { 0x09, 0x4c },
            { 0x4c, 0x01 }, { 0xcc, 0x09 }, { 0x0c, 0x49 }, { 0x4e, 0x03 }, { 0xce, 0x0b }, { 0x0e, 0x4b },
            { 0x45, 0x09 }, { 0x05, 0x14 }, { 0x40, 0x0c }, { 0x00, 0x11 } };
        if (b == 0x3f) {
            answer(0x0c);
            m_feeding = 1;
            return true;
        }
        if (b == 0x85) {
            answer(0x1c);
            for (int i = 0; i < 8; ++i) answer(i == 0 && busyPolls-- > 0 ? 1 : 0);
            answer(0);
            return true;
        }
        if (b == 0x80) {
            answer(0x19);
            for (int i = 0; i < 8; ++i) answer(i < 4 ? uint8_t(air[size_t(i)]) : 0);
            answer(0);
            return true;
        }
        const auto a = kAnswers.find(b);
        assert(a != kAnswers.end());
        answer(a->second);
        if ((b & 0xf0) == 0xc0) {
            m_payload = true;
            m_announce = b;
        }
        return true;
    }
    std::optional<uint8_t> read(int) override {
        if (m_out.empty()) return std::nullopt;
        const uint8_t b = m_out.front();
        m_out.pop_front();
        return b;
    }
    void flushInput() override {
        m_out.clear();
        m_payload = false;
        m_buffer.clear();
        m_feeding = 0;
    }

private:
    void answer(uint8_t b) {
        if (refuse > 0) {
            --refuse;
            b = uint8_t(b ^ 0x5a);
        }
        m_out.push_back(b);
    }
    std::deque<uint8_t> m_out;
    bool                m_payload = false;
    uint8_t             m_announce = 0;
    std::vector<uint8_t> m_buffer;
    int                 m_feeding = 0;
};

int32_t int32At(const std::vector<uint8_t>& b, size_t at) {
    return int32_t(uint32_t(b[at]) | uint32_t(b[at + 1]) << 8 | uint32_t(b[at + 2]) << 16 | uint32_t(b[at + 3]) << 24);
}

} // namespace

int main() {
    // OpenPnP's checksum: CRC-16/CCITT ("123456789" is 0x31C3), its low byte.
    const uint8_t digits[] = { '1', '2', '3', '4', '5', '6', '7', '8', '9' };
    assert(JPNeoden4Protocol::checksum(digits, sizeof digits) == 0xC3);

    FakeNeoden machine;
    JPNeoden4Protocol p(machine, 50);
    std::string why;

    // Homing: each nozzle up and turned back, let go, then the home command, done once ready.
    machine.busyPolls = 2;
    assert(p.home(why));
    assert(machine.payloads.size() == 10 && machine.payloads.back().announce == 0xc7 && machine.payloads.back().bytes[0] == 1);
    assert(machine.payloads[8].announce == 0xc1 && machine.payloads[8].bytes[3] == 0);   // every rotation let go

    // A move in steps, little-endian, negatives as two's complement.
    machine.payloads.clear();
    assert(p.moveSteps(12345, -678, why));
    assert(machine.payloads.size() == 1 && machine.payloads[0].announce == 0xc8);
    assert(int32At(machine.payloads[0].bytes, 0) == 12345 && int32At(machine.payloads[0].bytes, 4) == -678);

    // Z: its depth in microns; C: -tenths of a degree; the speed 10 .. 130.
    machine.payloads.clear();
    assert(p.moveZ(3, -4.5, why) && p.moveC(2, 90, why) && p.setMoveSpeed(0.5, why));
    const auto& z = machine.payloads[0].bytes;
    assert((z[0] | z[1] << 8) == 4500 && z[2] == 0x64 && z[3] == 3);
    const auto& c = machine.payloads[1].bytes;
    assert(int16_t(c[0] | c[1] << 8) == -900 && c[2] == 0x32 && c[3] == 2);
    const auto& v = machine.payloads[2].bytes;
    assert((v[0] | v[1] << 8) == 70 && v[2] == 0x09 && v[4] == 0xc8);

    // A peeler: the bottom half's by its id, the top half's from 20, counted from 1 there.
    machine.payloads.clear();
    assert(p.peel(5, 30, 40, why) && p.peel(21, 30, 40, why));
    assert(machine.payloads[0].announce == 0xcc && machine.payloads[0].bytes[0] == 5 && machine.payloads[0].bytes[1] == 40);
    assert(machine.payloads[1].announce == 0xce && machine.payloads[1].bytes[0] == 2 && machine.payloads[1].bytes[2] == 30);

    // A feeder fed: its strength and rate, unannounced after its id; its id changed (0..99 only).
    machine.payloads.clear();
    assert(p.feed(5, 50, 4, why));
    assert(machine.payloads.size() == 1 && machine.payloads[0].announce == 0x3f && machine.payloads[0].bytes[0] == 50
           && machine.payloads[0].bytes[1] == 4);
    assert(std::count(machine.written.end() - 6, machine.written.end(), uint8_t(0x46 + 5)) >= 1);
    assert(p.changeFeederId(5, 7, why) && machine.payloads.back().bytes[0] == 7 && machine.payloads.back().bytes[7] == 1);
    assert(!p.changeFeederId(5, 100, why) && why == "changeFeederId newId must be between 0-99.");

    // The air: full vacuum sent as 0x80; read back, a reading past 110 as below -128.
    machine.payloads.clear();
    assert(p.setAir(1, -128, why) && machine.payloads[0].bytes[0] == 0x80 && machine.payloads[0].bytes[1] == 1);
    int air = 0;
    assert(p.readAir(1, air, why) && air == -100);
    assert(p.readAir(4, air, why) && air == -136);

    // A spoilt answer: made again, and done.
    machine.payloads.clear();
    machine.refuse = 1;
    assert(p.lightsDown(3, why));
    assert(!machine.payloads.empty() && machine.payloads.back().announce == 0xc4 && machine.payloads.back().bytes[0] == 3);
    // A move is not made again by itself: its caller does.
    machine.refuse = 1;
    assert(!p.moveZ(1, 0, why) && !why.empty());
    machine.flushInput();
    return 0;
}
