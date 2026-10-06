// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

// A stand-in NeoDen 4 for the tests of its protocol and link.
#include <cassert>

#include "machine/JPNeoden4Protocol.h"

#include <array>
#include <deque>
#include <map>
#include <vector>

inline namespace jf {

// A NeoDen 4 as OpenPnP's driver sees it: each byte answered, each payload kept.
class FakeNeoden4 : public JPNeoden4Protocol::Port {
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

// The little-endian 32-bit number at `at` of a payload.
inline int32_t int32At(const std::vector<uint8_t>& b, size_t at) {
    return int32_t(uint32_t(b[at]) | uint32_t(b[at + 1]) << 8 | uint32_t(b[at + 2]) << 16 | uint32_t(b[at + 3]) << 24);
}


} // inline namespace jf
