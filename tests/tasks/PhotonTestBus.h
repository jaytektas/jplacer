// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

// OpenPnP's TestBus (its Photon tests' mock bus): replies set up for packets (by their content), the packets sent
// kept, and verified in order. A packet not set up, or set up with no reply, throws NoPacketMocking; a verification
// that fails throws AssertionFailed.

#include "tasks/JPPhotonBusInterface.h"

#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace photon_test {

using jf::JPPhotonPacket;

struct NoPacketMocking : std::runtime_error {
    using std::runtime_error::runtime_error;
};
struct AssertionFailed : std::runtime_error {
    using std::runtime_error::runtime_error;
};

class TestBus : public jf::JPPhotonBusInterface {
public:
    class Reply {
    public:
        explicit Reply(JPPhotonPacket packet) : m_packet(std::move(packet)) {}
        // The reply: the test's own packet, its packet id then set to the command's (as OpenPnP's reference).
        void reply(JPPhotonPacket& response) {
            m_response = &response;
            m_specified = true;
        }
        void timeout() {
            m_response = nullptr;
            m_specified = true;
        }
        bool isCommand(const JPPhotonPacket& p) const {
            return m_packet.toAddress == p.toAddress && m_packet.fromAddress == p.fromAddress && m_packet.payload == p.payload;
        }
        std::optional<JPPhotonPacket> packet(int packetId) {
            if (!m_specified) throw NoPacketMocking("Packet sent was specified but no reply given.");
            if (!m_response) return std::nullopt;
            m_response->packetId = packetId;
            return *m_response;
        }

    private:
        JPPhotonPacket  m_packet;
        JPPhotonPacket* m_response = nullptr;
        bool            m_specified = false;
    };

    class ContinuedVerification {
    public:
        explicit ContinuedVerification(const std::vector<JPPhotonPacket>& calls) : m_calls(calls) {}
        ContinuedVerification& then(const JPPhotonPacket& expected) {
            if (m_index >= m_calls.size()) throw AssertionFailed("No more calls on this test bus, but wanted " + expected.toByteString());
            const JPPhotonPacket& actual = m_calls[m_index];
            if (actual.toByteString() != expected.toByteString())
                throw AssertionFailed("Did not find expected packet " + expected.toByteString() + ", was " + actual.toByteString());
            ++m_index;
            return *this;
        }
        void nothingElseSent() {
            if (m_calls.size() <= m_index) return;
            const size_t more = m_calls.size() - m_index;
            std::string text = std::string("There ") + (more == 1 ? "is" : "are") + " still " + std::to_string(more) + " more call"
                             + (more == 1 ? "" : "s") + ":\n";
            for (size_t i = 0; i < more; ++i) text += "- " + m_calls[m_index + i].toByteString() + "\n";
            throw AssertionFailed(text);
        }
        bool hasMore() const { return m_index < m_calls.size(); }

    private:
        const std::vector<JPPhotonPacket>& m_calls;
        size_t                             m_index = 0;
    };

    std::optional<JPPhotonPacket> send(JPPhotonPacket command, std::string&) override {
        m_calls.push_back(command);
        for (const auto& r : m_replies)
            if (r->isCommand(command)) return r->packet(command.packetId);
        throw NoPacketMocking("Command packet did not match any requested mock.");
    }
    std::optional<JPPhotonPacket> send(const JPPhotonPacket& command) {
        std::string why;
        return send(command, why);
    }

    Reply& when(const JPPhotonPacket& packet) {
        m_mocked.push_back(packet);
        for (const auto& r : m_replies)
            if (r->isCommand(packet)) return *r;
        m_replies.push_back(std::make_unique<Reply>(packet));
        return *m_replies.back();
    }
    ContinuedVerification& verify(const JPPhotonPacket& packet) { return m_verification.then(packet); }
    void verifyNothingSent() { m_verification.nothingElseSent(); }
    void verifyInMockedOrder() {
        for (const JPPhotonPacket& p : m_mocked) m_verification.then(p);
        m_verification.nothingElseSent();
    }

private:
    std::vector<std::unique_ptr<Reply>> m_replies;
    std::vector<JPPhotonPacket>         m_calls;
    std::vector<JPPhotonPacket>         m_mocked;
    ContinuedVerification               m_verification { m_calls };
};

} // namespace photon_test
