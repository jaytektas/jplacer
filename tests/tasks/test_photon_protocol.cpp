// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// OpenPnP's Photon protocol tests: PacketTest (written and read, its length and CRC; refused when empty, odd, of the
// wrong CRC or length, or TIMEOUT), PhotonBusTest (sent as the data actuator's read parameter, its number matched,
// numbered in turn and rolling over), each command's test (its packet, and its reply read: valid, its error, its
// hardware id or time to feed; not valid at another length), ResponsesHelperTest and TestBusTest (the tests' own
// replies and mock bus).
// Tests check with assert(); a Release build must not compile it away.
#undef NDEBUG
#include <cassert>

#include "PhotonResponses.h"
#include "PhotonTestBus.h"
#include "tasks/JPPhotonBus.h"
#include "tasks/JPPhotonCommands.h"

#include <functional>
#include <string>
#include <vector>

using namespace jf;
using namespace photon_test;

namespace {

using C = JPPhotonCommands;
using E = JPPhotonCommands::Error;
using Bytes = std::vector<uint8_t>;

const std::string kUuid = "FFEEDDCCBBAA998877665544";
const Bytes kUuidBytes { 0xFF, 0xEE, 0xDD, 0xCC, 0xBB, 0xAA, 0x99, 0x88, 0x77, 0x66, 0x55, 0x44 };

Bytes withUuid(uint8_t first, size_t uuidBytes = 12, std::optional<uint8_t> after = std::nullopt) {
    Bytes b { first };
    b.insert(b.end(), kUuidBytes.begin(), kUuidBytes.begin() + long(uuidBytes));
    if (after) b.push_back(*after);
    return b;
}

// A reply from `from` with `payload` (OpenPnP's test packets: to 0, packet id 0, its CRC worked out).
JPPhotonPacket reply(int from, Bytes payload) {
    JPPhotonPacket p;
    p.fromAddress = from;
    p.payload = std::move(payload);
    return p;
}

template <typename Ex>
bool throws(const std::function<void()>& fn) {
    try {
        fn();
    } catch (const Ex&) {
        return true;
    }
    return false;
}

// ---- PacketTest

void packetTest() {
    // callingToByteStringAutomaticallySetsPayloadLengthAndCrC
    JPPhotonPacket packet;
    packet.toAddress = 0x2B;
    packet.fromAddress = 0x13;
    packet.packetId = 0x47;
    packet.payload = { 0x03 };
    assert(packet.toByteString() == "2B1347010A03");
    assert(packet.payload.size() == 0x01 && packet.crc() == 0x0A);
    // cloningPacket
    const JPPhotonPacket copy = packet;
    assert(&copy != &packet && copy.toAddress == packet.toAddress && copy.fromAddress == packet.fromAddress
           && copy.packetId == packet.packetId && copy.crc() == packet.crc() && copy.payload == packet.payload
           && copy.payload.data() != packet.payload.data());
    // decodingValidPacket
    const auto valid = JPPhotonPacket::decode("2B1347010A03");
    assert(valid && valid->toAddress == 0x2B && valid->fromAddress == 0x13 && valid->packetId == 0x47
           && valid->payload == Bytes { 0x03 } && valid->crc() == 0x0A);
    // decodingEmptyString, decodingOddLengthString
    assert(!JPPhotonPacket::decode(""));
    assert(!JPPhotonPacket::decode("01234"));
    // decodingPacketWithBadChecksum: the real checksum is 0xC7, 0xC9 here
    assert(!JPPhotonPacket::decode("2B000001C903"));
    // decodingPacketWithTooShortLength, decodingPacketWithTooLongLength: the checksum valid for the wrong length
    assert(!JPPhotonPacket::decode("2B000000D203"));
    assert(!JPPhotonPacket::decode("2B000002F803"));
    // decodingTimeout
    assert(!JPPhotonPacket::decode("TIMEOUT"));
}

// ---- PhotonBusTest: the data actuator mocked, answering with `response` (its packet id and CRC made the command's).

struct MockedActuator {
    std::vector<std::string> reads;
    std::function<std::string(const std::string&)> answer;
    JPPhotonBus::Read read() {
        return [this](const std::string& parameter, std::string& value, std::string&) {
            reads.push_back(parameter);
            value = answer(parameter);
            return true;
        };
    }
    void matchPacketId(const JPPhotonPacket& response) {
        answer = [response = JPPhotonPacket(response)](const std::string& command) mutable {
            const auto c = JPPhotonPacket::decode(command);
            if (!c) return std::string();
            response.packetId = c->packetId;
            return response.toByteString();
        };
    }
};

void photonBusTest() {
    const Responses responses(0);
    std::string why;
    // busSendsPacketInStringForm
    {
        MockedActuator actuator;
        JPPhotonBus bus(0, actuator.read());
        JPPhotonPacket command = C::getFeederId(0x42);
        const JPPhotonPacket generated = responses.getFeederIdOk(0x42, kUuid);
        actuator.matchPacketId(generated);
        const auto response = bus.send(command, why);
        assert(response);
        // What was sent: the command as the bus numbered it (from 0, packet 0).
        command.fromAddress = 0;
        command.packetId = 0;
        assert(actuator.reads == std::vector<std::string> { command.toByteString() });
        JPPhotonPacket expected = generated;
        expected.packetId = command.packetId;
        assert(response->toByteString() == expected.toByteString());
        assert(response->toAddress == generated.toAddress && response->fromAddress == generated.fromAddress
               && response->packetId == command.packetId && response->payload == generated.payload);
    }
    // busReturnsEmptyOptionalIfTimeoutOccurs
    {
        MockedActuator actuator;
        actuator.answer = [](const std::string&) { return std::string("TIMEOUT"); };
        JPPhotonBus bus(0, actuator.read());
        assert(!bus.send(C::getFeederId(0x42), why));
    }
    // busReturnsEmptyOptionalIfWrongPacketId
    {
        MockedActuator actuator;
        JPPhotonPacket generated = responses.getFeederIdOk(0x42, kUuid);
        generated.packetId = 0x47;
        actuator.answer = [generated](const std::string&) { return generated.toByteString(); };
        JPPhotonBus bus(0, actuator.read());
        assert(!bus.send(C::getFeederId(0x42), why));
    }
    // busIncrementsPacketId
    {
        MockedActuator actuator;
        actuator.matchPacketId(responses.getFeederIdOk(0x42, kUuid));
        JPPhotonBus bus(0, actuator.read());
        const auto first = bus.send(C::getFeederId(0x42), why);
        const auto second = bus.send(C::getFeederId(0x42), why);
        assert(first && second && first->packetId == 0 && second->packetId == 1);
        assert(JPPhotonPacket::decode(actuator.reads[0])->packetId == first->packetId);
    }
    // busPacketIdRollsOver
    {
        MockedActuator actuator;
        actuator.matchPacketId(responses.getFeederIdOk(0x42, kUuid));
        JPPhotonBus bus(0, actuator.read());
        std::optional<JPPhotonPacket> response;
        for (int i = 0; i < 256; ++i) response = bus.send(C::getFeederId(0x42), why);
        assert(response && response->packetId == 0xff);   // the last command put it in a state to roll over
        response = bus.send(C::getFeederId(0x42), why);   // this one rolls over to 0
        assert(response && response->packetId == 0);
    }
}

// ---- the commands' tests

void assertCommand(const JPPhotonPacket& p, int to, const Bytes& payload) {
    assert(p.toAddress == to && p.payload.size() == payload.size() && p.payload == payload);
}

void assertReply(const C::Response& r, int from, bool valid, std::optional<E> error = std::nullopt) {
    assert(r.toAddress == 0 && r.fromAddress == from && r.valid == valid && r.error == error);
}

void getFeederAddressTest() {
    assertCommand(C::getFeederAddress(kUuid), 0xff, withUuid(0xC0));
    assertReply(C::decode(C::GetFeederAddressId, reply(24, { 0x00 })), 24, true, E::None);
    assertReply(C::decode(C::GetFeederAddressId, reply(17, {})), 17, false);
    assertReply(C::decode(C::GetFeederAddressId, reply(17, { 0x00, 0x00 })), 17, false);
}

void getFeederIdTest() {
    assertCommand(C::getFeederId(68), 68, { 0x01 });
    const auto ok = C::decode(C::GetFeederIdId, reply(17, withUuid(0x00)));
    assertReply(ok, 17, true, E::None);
    assert(ok.uuid == kUuid);
    const auto shorter = C::decode(C::GetFeederIdId, reply(17, withUuid(0x00, 11)));
    assertReply(shorter, 17, false);
    assert(!shorter.uuid);
    const auto longer = C::decode(C::GetFeederIdId, reply(17, withUuid(0x00, 12, 0xFF)));
    assertReply(longer, 17, false);
    assert(!longer.uuid);
}

void getVersionTest() {
    assertCommand(C::getVersion(43), 43, { 0x03 });
    assertReply(C::decode(C::GetVersionId, reply(23, { 0x00 })), 23, true, E::None);
    assertReply(C::decode(C::GetVersionId, reply(17, {})), 17, false);
    assertReply(C::decode(C::GetVersionId, reply(17, { 0x00, 0x00 })), 17, false);
}

void identifyFeederTest() {
    assertCommand(C::identifyFeeder(kUuid), 0xff, withUuid(0xC1));
    assertReply(C::decode(C::IdentifyFeederId, reply(24, { 0x00 })), 24, true, E::None);
    assertReply(C::decode(C::IdentifyFeederId, reply(17, {})), 17, false);
    assertReply(C::decode(C::IdentifyFeederId, reply(17, { 0x00, 0x00 })), 17, false);
}

void initializeFeederTest() {
    assertCommand(C::initializeFeeder(39, kUuid), 39, withUuid(0x02));
    const auto ok = C::decode(C::InitializeFeederId, reply(24, withUuid(0x00)));
    assertReply(ok, 24, true, E::None);
    assert(ok.uuid == kUuid);
    const auto wrong = C::decode(C::InitializeFeederId, reply(39, withUuid(0x01)));
    assertReply(wrong, 39, true, E::WrongFeederUuid);
    assert(wrong.uuid == kUuid);
    const auto shorter = C::decode(C::InitializeFeederId, reply(17, withUuid(0x00, 11)));
    assertReply(shorter, 17, false);
    assert(!shorter.uuid);
    const auto longer = C::decode(C::InitializeFeederId, reply(17, withUuid(0x00, 12, 0xFF)));
    assertReply(longer, 17, false);
    assert(!longer.uuid);
}

void moveFeedBackwardTest() {
    assertCommand(C::moveFeedBackward(29, 20), 29, { 0x05, 0x14 });
    const auto ok = C::decode(C::MoveFeedBackwardId, reply(23, { 0x00, 0x01, 0x02 }));
    assertReply(ok, 23, true, E::None);
    assert(ok.expectedTimeToFeed == 258);
    const auto uninitialized = C::decode(C::MoveFeedBackwardId, reply(39, { 0x03, 0x00, 0x00 }));
    assertReply(uninitialized, 39, true, E::UninitializedFeeder);
    assert(uninitialized.expectedTimeToFeed == 0);
    const auto fault = C::decode(C::MoveFeedBackwardId, reply(39, { 0x02, 0x00, 0x00 }));
    assertReply(fault, 39, true, E::CouldNotReach);
    assert(fault.expectedTimeToFeed == 0);
    const auto shorter = C::decode(C::MoveFeedBackwardId, reply(17, { 0x00, 0x00 }));
    assertReply(shorter, 17, false);
    assert(shorter.expectedTimeToFeed == 0);
    assertReply(C::decode(C::MoveFeedBackwardId, reply(17, { 0x00, 0x00, 0x00, 0x00 })), 17, false);
}

void moveFeedForwardTest() {
    assertCommand(C::moveFeedForward(29, 20), 29, { 0x04, 0x14 });
    const auto ok = C::decode(C::MoveFeedForwardId, reply(23, { 0x00, 0x01, 0x02 }));
    assertReply(ok, 23, true, E::None);
    assert(ok.expectedTimeToFeed == 258);
    const auto uninitialized = C::decode(C::MoveFeedForwardId, reply(39, { 0x03, 0x00, 0x00 }));
    assertReply(uninitialized, 39, true, E::UninitializedFeeder);
    assert(uninitialized.expectedTimeToFeed == 0);
    const auto fault = C::decode(C::MoveFeedForwardId, reply(39, { 0x02, 0x00, 0x00 }));
    assertReply(fault, 39, true, E::CouldNotReach);
    assert(fault.expectedTimeToFeed == 0);
}

void moveFeedStatusTest() {
    assertCommand(C::moveFeedStatus(29), 29, { 0x06 });
    assertReply(C::decode(C::MoveFeedStatusId, reply(23, { 0x00 })), 23, true, E::None);
    assertReply(C::decode(C::MoveFeedStatusId, reply(39, { 0x03 })), 39, true, E::UninitializedFeeder);
    assertReply(C::decode(C::MoveFeedStatusId, reply(39, { 0x02 })), 39, true, E::CouldNotReach);
    assertReply(C::decode(C::MoveFeedStatusId, reply(39, { 0x04 })), 39, true, E::FeedingInProgress);
    assertReply(C::decode(C::MoveFeedStatusId, reply(17, {})), 17, false);
    assertReply(C::decode(C::MoveFeedStatusId, reply(17, { 0x00, 0x00 })), 17, false);
}

void programFeederFloorAddressTest() {
    assertCommand(C::programFeederFloorAddress(kUuid, 93), 0xff, withUuid(0xC2, 12, 93));
    assertReply(C::decode(C::ProgramFeederFloorAddressId, reply(24, { 0x00 })), 24, true, E::None);
    assertReply(C::decode(C::ProgramFeederFloorAddressId, reply(17, {})), 17, false);
    assertReply(C::decode(C::ProgramFeederFloorAddressId, reply(17, { 0x00, 0x00 })), 17, false);
}

void uninitializedFeedersRespondTest() {
    assertCommand(C::uninitializedFeedersRespond(), 0xff, { 0xC3 });
    const auto ok = C::decode(C::UninitializedFeedersRespondId, reply(0, withUuid(0x00)));
    assertReply(ok, 0, true, E::None);
    assert(ok.uuid == kUuid);
    assertReply(C::decode(C::UninitializedFeedersRespondId, reply(0, withUuid(0x00, 11))), 0, false);
    assertReply(C::decode(C::UninitializedFeedersRespondId, reply(0, withUuid(0x00, 12, 0xFF))), 0, false);
}

// ---- ResponsesHelperTest

void responsesHelperTest() {
    const Responses r(0);
    // ErrorsTest
    const JPPhotonPacket wrong = r.wrongFeederUUID(3, kUuid);
    assert(wrong.fromAddress == 3 && wrong.payload == withUuid(0x01));
    const JPPhotonPacket fault = r.couldNotReach(7);
    assert(fault.fromAddress == 7 && fault.payload == Bytes { 0x02 });
    const JPPhotonPacket uninitialized = r.uninitializedFeeder(11, kUuid);
    assert(uninitialized.toAddress == 0 && uninitialized.fromAddress == 11 && uninitialized.payload == withUuid(0x03));
    // the replies
    for (const JPPhotonPacket& p : { r.getFeederIdOk(17, kUuid), r.initializeFeederOk(17, kUuid) })
        assert(p.fromAddress == 17 && p.packetId == 0 && p.payload == withUuid(0x00));
    const JPPhotonPacket version = r.getVersionOk(20, 1);
    assert(version.fromAddress == 20 && version.packetId == 0 && version.payload == (Bytes { 0x00, 0x01 }));
    for (const JPPhotonPacket& p : { r.moveFeedForwardOk(20, 258), r.moveFeedBackwardOk(20, 258) })
        assert(p.fromAddress == 20 && p.packetId == 0 && p.payload == (Bytes { 0x00, 0x01, 0x02 }));
    for (const JPPhotonPacket& p : { r.moveFeedStatusOk(20), r.getFeederAddressOk(20), r.identifyFeederOk(20), r.programFeederFloorOk(20) })
        assert(p.fromAddress == 20 && p.packetId == 0 && p.payload == Bytes { 0x00 });
}

// ---- TestBusTest

void testBusTest() {
    const Responses responses(0);
    const JPPhotonPacket getVersion47 = C::getVersion(0x47);
    // busRespondsWithReplyToRequestedCommandPacket, busRespondsWithReplyToRequestedCommand
    {
        TestBus bus;
        JPPhotonPacket response = responses.getVersionOk(0x47, 3);
        bus.when(getVersion47).reply(response);
        const auto got = bus.send(getVersion47);
        assert(got && got->toByteString() == response.toByteString());
    }
    // busRespondsWithTimeout
    {
        TestBus bus;
        bus.when(getVersion47).timeout();
        assert(!bus.send(getVersion47));
    }
    // busRespondsWithExceptionIfCommandHasNoReply, busRespondsWithExceptionIfCommandIsNotMockedAtAll
    {
        TestBus bus;
        bus.when(getVersion47);
        assert(throws<NoPacketMocking>([&] { bus.send(getVersion47); }));
        TestBus none;
        assert(throws<NoPacketMocking>([&] { none.send(getVersion47); }));
    }
    // busValidatesToAddress, busValidatesFromAddress, busValidatesPayloadLength, busValidatesPayloadDataLength,
    // busValidatesPayloadData (a packet's length is its payload's here, so a longer payload stands for both lengths)
    for (const auto& change : std::vector<std::function<void(JPPhotonPacket&)>> {
             [](JPPhotonPacket& p) { p.toAddress = 0x10; }, [](JPPhotonPacket& p) { p.fromAddress = 0x10; },
             [](JPPhotonPacket& p) { p.payload = { 0x03, 0x05 }; }, [](JPPhotonPacket& p) { p.payload = { 0x04 }; } }) {
        TestBus bus;
        JPPhotonPacket mocking = getVersion47;
        change(mocking);
        bus.when(mocking).timeout();
        assert(throws<NoPacketMocking>([&] { bus.send(getVersion47); }));
    }
    // busAdjustsResponsePacketId
    {
        TestBus bus;
        JPPhotonPacket command = getVersion47;
        command.packetId = 0x43;
        JPPhotonPacket response = responses.getVersionOk(0x47, 3);
        bus.when(command).reply(response);
        assert(response.packetId == 0);
        const auto got = bus.send(command);
        assert(got && response.packetId == 0x43 && got->toByteString() == response.toByteString());
    }
    // busCanHandleRespondingDifferentlyToDifferentSendingPackets
    {
        TestBus bus;
        JPPhotonPacket first = responses.getVersionOk(0x01, 3), second = responses.getVersionOk(0x02, 4);
        bus.when(C::getVersion(0x01)).reply(first);
        bus.when(C::getVersion(0x02)).reply(second);
        assert(bus.send(C::getVersion(0x01))->toByteString() == first.toByteString());
        assert(bus.send(C::getVersion(0x02))->toByteString() == second.toByteString());
    }
    // busWillOverrideReplyIfCommandIsSpecifiedAgain: by the packets' content, not the same object
    {
        TestBus bus;
        JPPhotonPacket response = responses.getVersionOk(5, 1);
        bus.when(C::getVersion(5)).reply(response);
        assert(bus.send(C::getVersion(5))->toByteString() == response.toByteString());
        bus.when(C::getVersion(5)).timeout();
        assert(!bus.send(C::getVersion(5)));
    }
    // busThrowsAssertionFailedErrorIfCommandNotInvoked, busDoesNotThrowAssertionFailedErrorIfCommandIsInvoked
    {
        TestBus bus;
        bus.when(C::getVersion(5)).timeout();
        assert(throws<AssertionFailed>([&] { bus.verify(C::getVersion(5)); }));
        TestBus invoked;
        invoked.when(C::getVersion(5)).timeout();
        invoked.send(C::getVersion(5));
        invoked.verify(C::getVersion(5));
    }
    auto twoMocked = [](TestBus& bus) {
        bus.when(C::getVersion(5)).timeout();
        bus.when(C::getVersion(10)).timeout();
    };
    // busVerifiesContentOfCalls
    {
        TestBus bus;
        twoMocked(bus);
        bus.send(C::getVersion(10));
        assert(throws<AssertionFailed>([&] { bus.verify(C::getVersion(5)); }));
    }
    // busVerifiesOrderOfCalls, busWillFailWithNoMoreCalls, canVerifyCallsThenVerifyNoMore
    {
        TestBus bus;
        twoMocked(bus);
        bus.send(C::getVersion(5));
        bus.send(C::getVersion(10));
        bus.verify(C::getVersion(5)).then(C::getVersion(10));
        TestBus more;
        twoMocked(more);
        more.send(C::getVersion(5));
        more.send(C::getVersion(10));
        assert(throws<AssertionFailed>([&] { more.verify(C::getVersion(5)).nothingElseSent(); }));
        TestBus all;
        twoMocked(all);
        all.send(C::getVersion(5));
        all.send(C::getVersion(10));
        all.verify(C::getVersion(5)).then(C::getVersion(10)).nothingElseSent();
    }
    // canImmediatelyVerifyNoMoreCalls, canImmediatelyFailIfSomethingSentAndNothingSent
    {
        TestBus bus;
        twoMocked(bus);
        bus.verifyNothingSent();
        TestBus sent;
        sent.when(C::getVersion(5)).timeout();
        sent.send(C::getVersion(5));
        assert(throws<AssertionFailed>([&] { sent.verifyNothingSent(); }));
    }
    // callVerificationStillWorksIfReplyIsChanged
    {
        TestBus bus;
        JPPhotonPacket response = responses.getVersionOk(0x47, 3);
        bus.when(C::getVersion(5)).timeout();
        bus.send(C::getVersion(5));
        bus.when(C::getVersion(5)).reply(response);
        bus.send(C::getVersion(5));
        bus.verify(C::getVersion(5)).then(C::getVersion(5)).nothingElseSent();
    }
    // canVerifyInMockedOrder, willFailVerifyInMockedOrder
    {
        TestBus bus;
        twoMocked(bus);
        bus.send(C::getVersion(5));
        bus.send(C::getVersion(10));
        bus.verifyInMockedOrder();
        TestBus reversed;
        twoMocked(reversed);
        reversed.send(C::getVersion(10));
        reversed.send(C::getVersion(5));
        assert(throws<AssertionFailed>([&] { reversed.verifyInMockedOrder(); }));
    }
    // whenReliesOnPacketContentEvenIfContentChanges, verifyReliesOnPacketContentEvenIfContentChanges,
    // verifyMockedOrderReliesOnPacketContentEvenIfContentChanges
    {
        TestBus bus;
        JPPhotonPacket packet = C::getVersion(5);
        bus.when(packet).timeout();
        packet.toAddress = 10;   // no longer what was mocked
        assert(throws<NoPacketMocking>([&] { bus.send(packet); }));
        TestBus verified;
        JPPhotonPacket sent = C::getVersion(5);
        verified.when(sent).timeout();
        verified.send(sent);
        sent.toAddress = 10;
        assert(throws<AssertionFailed>([&] { verified.verify(sent); }));
        TestBus ordered;
        JPPhotonPacket mocked = C::getVersion(5);
        ordered.when(mocked).timeout();
        ordered.send(mocked);
        mocked.toAddress = 10;   // sent what was mocked, then changed
        ordered.verifyInMockedOrder();
    }
    // verifyInMockedOrderCallsNothingElseSent
    {
        TestBus bus;
        twoMocked(bus);
        bus.send(C::getVersion(5));
        bus.send(C::getVersion(10));
        bus.send(C::getVersion(5));
        assert(throws<AssertionFailed>([&] { bus.verifyInMockedOrder(); }));
    }
}

} // namespace

int main() {
    packetTest();
    photonBusTest();
    getFeederAddressTest();
    getFeederIdTest();
    getVersionTest();
    identifyFeederTest();
    initializeFeederTest();
    moveFeedBackwardTest();
    moveFeedForwardTest();
    moveFeedStatusTest();
    programFeederFloorAddressTest();
    uninitializedFeedersRespondTest();
    responsesHelperTest();
    testBusTest();
    return 0;
}
