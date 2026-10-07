// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// OpenPnP's PhotonFeederTest, PhotonFeederSlotsTest, PhotonPropertiesTest and PhotonFeederLoadingTest: a Photon
// feeder's name and slot, when it is enabled, found, initialized and prepared for a job (found again when lost, the
// feeder answering in its place made or found), fed (initialized again, retried as the machine's Photon settings
// say, its status asked until it is done), every feeder found, its issues, its pick location; the machine's slots
// and settings; and its data actuator. On OpenPnP's mock bus (TestBus): what is sent checked packet by packet.
// Tests check with assert(); a Release build must not compile it away.
#undef NDEBUG
#include <cassert>

#include "FakeJobMachine.h"
#include "PhotonResponses.h"
#include "PhotonTestBus.h"
#include "model/JPConfiguration.h"
#include "openpnp/JPXmlReader.h"
#include "openpnp/JPXmlWriter.h"
#include "setup/JPFeederForms.h"
#include "setup/JPIssueChecks.h"
#include "setup/JPSolutions.h"
#include "tasks/JPFeederFeed.h"
#include "tasks/JPPhotonBus.h"
#include "tasks/JPPhotonFeeders.h"

#include <filesystem>
#include <functional>
#include <map>
#include <random>
#include <string>
#include <unistd.h>
#include <vector>

using namespace jf;
using namespace photon_test;
namespace fs = std::filesystem;

namespace {

using C = JPPhotonCommands;
constexpr const char* kPhotonClass = "org.openpnp.machine.photon.PhotonFeeder";
const std::string kHardwareId = "00112233445566778899AABB";
constexpr int kFeederAddress = 5;
const JPLocation kBaseLocation(JPLengthUnit::Millimeters, 1, 2, 3, 0);
const JPLocation kFeederOffset(JPLengthUnit::Millimeters, 1, 1, 0, 45);

template <typename Ex>
bool throws(const std::function<void()>& fn) {
    try {
        fn();
    } catch (const Ex&) {
        return true;
    }
    return false;
}

// OpenPnP's setUp: a fresh configuration with one Photon feeder, the machine's, and the mock bus in place of its.
struct Bench {
    fs::path        dir;
    JPConfiguration config;
    FakeJobMachine  machine;
    TestBus         bus;
    Responses       responses { 0 };
    std::string     feeder;   // the feeder's id
    std::mt19937    random { 1 };

    Bench() : dir(fs::temp_directory_path() / ("jplacer-photon-" + std::to_string(::getpid()))), config(dir.string()) {
        feeder = add();
        JPPhotonFeeders::setBus(&bus);
    }
    ~Bench() {
        JPPhotonFeeders::setBus(nullptr);
        fs::remove_all(dir);
    }
    std::string add() {
        JPFeeder f = JPFeeder::create(kPhotonClass, "");
        const std::string id = f.id();
        config.addFeeder(std::move(f));
        return id;
    }
    JPFeeder& f(const std::string& id) { return *config.feeder(id); }
    JPFeeder& f() { return f(feeder); }

    // The feeder's properties, as OpenPnP's setters.
    void setHardwareId(const std::string& id, const std::string& hw) { f(id).setHardwareId(hw); }
    void setHardwareId(const std::string& hw) { setHardwareId(feeder, hw); }
    void setOffset(const std::string& id, const JPLocation& l) { f(id).setLocationOf("offset", l); }
    void setOffset(const JPLocation& l) { setOffset(feeder, l); }
    void setSlotAddress(const std::string& id, std::optional<int> a) { config.setPhotonSlot(id, a); }
    void setSlotAddress(std::optional<int> a) { setSlotAddress(feeder, a); }
    void setSlotLocation(int address, const JPLocation& l) {
        config.photon().setSlotLocation(address, l);
        config.resolvePhoton();
    }
    void setPartPitch(int pitch) { f().setNumber("part-pitch", pitch); }
    void setMoveWhileFeeding(const std::string& id, bool on) { f(id).setFlag("move-while-feeding", on); }
    void setMaxRetry(int n) { config.photon().setFeederCommunicationMaxRetry(n); }

    // OpenPnP's prepareForJob, feed and findSlotAddress: false, and why, where OpenPnP throws.
    bool prepareForJob(const std::string& id, std::string& why) {
        return JPPhotonFeeders::prepareForJob(config, id, machine, nullptr, why);
    }
    bool prepareForJob(std::string& why) { return prepareForJob(feeder, why); }
    bool feed(const std::string& id, std::string& why) {
        bool empty = false;
        return JPFeederFeed::feed(config, id, "N1", machine, nullptr, why, empty);
    }
    bool feed(std::string& why) { return feed(feeder, why); }
    void findSlotAddress() {
        std::string why;
        assert(JPPhotonFeeders::findSlotAddress(config, feeder, machine, nullptr, why));
    }
    void findAllFeeders() {
        std::string why;
        assert(JPPhotonFeeders::findAll(config, machine, nullptr, nullptr, why));
    }

    std::string randomUUID() {
        const char* hex = "0123456789ABCDEF";
        std::string out;
        for (int i = 0; i < 24; ++i) out += hex[random() % 16];
        return out;
    }
    // OpenPnP's findByHardwareId and findBySlotAddress.
    JPFeeder* byHardwareId(const std::string& hw) { return config.photonFeeder(hw); }
    JPFeeder* bySlotAddress(int address) {
        for (JPFeeder& x : config.feeders())
            if (x.isPhoton() && x.photonSlot == address) return &x;
        return nullptr;
    }
    // The issues OpenPnP's Solutions find about the feeder.
    const JPSolutions::Issue* issueAbout(const std::string& id) {
        JPCellConfig cell;
        JPIssueChecks::Context c;
        c.config = &config;
        c.cell = [&cell]() -> const JPCellConfig* { return &cell; };
        solutions.setChecks(JPIssueChecks::all(c));
        solutions.find();
        solutions.publish();
        for (const auto& i : solutions.issues())
            if (i->openpnpSubject == "PhotonFeeder " + f(id).name() && i->subject == "Feeder " + f(id).name()) return i.get();
        return nullptr;
    }
    JPSolutions solutions;
};

void getJobPreparationLocationReturnsNull() {
    // A job visits no Photon feeder before it starts (it is neither a vision tape feeder nor a blinds feeder).
    Bench b;
    assert(b.f().isPhoton() && !b.f().isVisionTape() && b.f().typeName() != "BlindsFeeder");
}

void getDataActuatorCreatesReferenceActuatorIfOneDoesNotExist() {
    JPCellConfig cell;
    assert(!cell.actuatorNamed(JPPhotonBus::kDataActuator));
    assert(JPPhotonFeeders::addDataActuator(cell));
    const JPActuatorConfig* a = cell.actuatorNamed(JPPhotonBus::kDataActuator);
    assert(a && a->name == JPPhotonBus::kDataActuator);
    assert(!JPPhotonFeeders::addDataActuator(cell) && cell.actuators.size() == 1);   // the same one, once
}

void getSlotAddressReturnsNullByDefault() {
    Bench b;
    assert(!b.f().photonSlot);
}

void isEnabled() {
    // isEnabledReturnsFalseIfSetEnabledToFalse
    {
        Bench b;
        b.f().setEnabled(false);
        assert(!b.f().enabled());
    }
    // isEnabledReturnsFalseIfNoHardwareIdSet
    {
        Bench b;
        b.f().setEnabled(true);
        assert(b.f().text("hardware-id").empty() && !b.f().enabled());
    }
    // isEnabledReturnsFalseIfNoPartIsSet
    {
        Bench b;
        b.f().setEnabled(true);
        b.setHardwareId(kHardwareId);
        assert(b.f().partId().empty() && !b.f().enabled());
    }
    // isEnabledReturnsFalseIfNoAddressIsSet
    {
        Bench b;
        b.f().setEnabled(true);
        b.setHardwareId(kHardwareId);
        b.f().setText("part-id", "test-part");
        assert(!b.f().photonSlot && !b.f().enabled());
    }
    // isEnabledReturnsFalseIfSlotHasNoLocation
    {
        Bench b;
        b.f().setEnabled(true);
        b.setHardwareId(kHardwareId);
        b.f().setText("part-id", "test-part");
        b.setSlotAddress(5);
        assert(!b.f().photonSlotLocation && !b.f().enabled());
    }
    // isEnabledReturnsFalseIfFeederHasNoOffset
    {
        Bench b;
        b.f().setEnabled(true);
        b.setHardwareId(kHardwareId);
        b.f().setText("part-id", "test-part");
        b.setSlotAddress(kFeederAddress);
        b.setSlotLocation(kFeederAddress, kBaseLocation);
        assert(!b.f().enabled());
    }
    // isEnabledReturnsTrueIfEverythingIsSet
    {
        Bench b;
        b.f().setEnabled(true);
        b.setHardwareId(kHardwareId);
        b.f().setText("part-id", "test-part");
        b.setSlotAddress(kFeederAddress);
        b.setSlotLocation(kFeederAddress, kBaseLocation);
        b.setOffset(kFeederOffset);
        assert(b.f().enabled());
    }
}

void names() {
    // getNameByDefaultReturnsClassSimpleName
    {
        Bench b;
        assert(b.f().name() == "Unconfigured PhotonFeeder");
    }
    // getNameUsesHardwareIdWhenThatIsSet
    {
        Bench b;
        b.setHardwareId(kHardwareId);
        assert(b.f().name() == kHardwareId + " (Slot: None)");
    }
    // getNameUsesHardwareIdAndSlotWhenBothAreSet
    {
        Bench b;
        b.setHardwareId(kHardwareId);
        b.setSlotAddress(27);
        assert(b.f().name() == kHardwareId + " (Slot: 27)");
    }
    // setHardwareIdOverridesNameOnlyIfItIsNotAlreadySet
    {
        Bench b;
        b.f().setName("My Name");
        b.setHardwareId(kHardwareId);
        assert(b.f().name() == "My Name (Slot: None)");
    }
    // setNameWorksWithoutSlotIncluded
    {
        Bench b;
        b.setHardwareId(kHardwareId);
        b.f().setName("Some Test Name");
        assert(b.f().name() == "Some Test Name (Slot: None)");
    }
    // setNameWorksWithSlotIncluded
    {
        Bench b;
        b.setHardwareId(kHardwareId);
        b.setSlotAddress(13);
        b.f().setName("Test Name (Slot: 13)");
        assert(b.f().name() == "Test Name (Slot: 13)");
    }
    // setNameWorksWithOverridingNoneSlot
    {
        Bench b;
        b.setHardwareId(kHardwareId);
        b.setSlotAddress(13);
        b.f().setName("Test Name (Slot: None)");
        assert(b.f().name() == "Test Name (Slot: 13)");
    }
    // setNameWorksWithUnexpectedSlotNumber
    {
        Bench b;
        b.setHardwareId(kHardwareId);
        b.setSlotAddress(3);
        b.f().setName("Test Name (Slot: 13)");
        assert(b.f().name() == "Test Name (Slot: 3)");
    }
    // setNameWithMalformedSlotKeepsMalformedSlot
    {
        Bench b;
        b.setHardwareId(kHardwareId);
        b.f().setName("Test Name (Slot: No");
        assert(b.f().name() == "Test Name (Slot: No (Slot: None)");
    }
    // setNameCorrectlyTrimsInputName
    {
        Bench b;
        b.setHardwareId(kHardwareId);
        b.f().setName("Test Name ");
        assert(b.f().name() == "Test Name (Slot: None)");
    }
    // setNameWithMultipleRandomSlots: the spaces inside not trimmed, as OpenPnP
    {
        Bench b;
        b.setHardwareId(kHardwareId);
        b.f().setName("This (Slot: 1) Is (Slot: 2) A (Slot: 3) Weird (Slot: None) Test");
        assert(b.f().name() == "This  Is  A  Weird  Test (Slot: None)");
    }
}

void isInitializedByDefaultReturnsFalse() {
    Bench b;
    assert(!b.f().photonInitialized);
}

void prepareForJob() {
    std::string why;
    // prepareForJobFindsFeederAddressAndInitializes
    {
        Bench b;
        b.setHardwareId(kHardwareId);
        b.setOffset(kFeederOffset);
        b.setSlotLocation(kFeederAddress, kBaseLocation);
        JPPhotonPacket address = b.responses.getFeederAddressOk(kFeederAddress), init = b.responses.initializeFeederOk(kFeederAddress, kHardwareId);
        b.bus.when(C::getFeederAddress(kHardwareId)).reply(address);
        b.bus.when(C::initializeFeeder(kFeederAddress, kHardwareId)).reply(init);
        assert(b.prepareForJob(why));
        assert(b.f().photonSlot == kFeederAddress && b.f().photonInitialized);
        b.bus.verifyInMockedOrder();
    }
    // prepareForJobInitializesIfSlotAddressIsSet
    {
        Bench b;
        b.setHardwareId(kHardwareId);
        b.setOffset(kFeederOffset);
        b.setSlotAddress(kFeederAddress);
        b.setSlotLocation(kFeederAddress, kBaseLocation);
        JPPhotonPacket init = b.responses.initializeFeederOk(kFeederAddress, kHardwareId);
        b.bus.when(C::initializeFeeder(kFeederAddress, kHardwareId)).reply(init);
        assert(b.prepareForJob(why));
        assert(b.f().photonSlot == kFeederAddress && b.f().photonInitialized);
        b.bus.verifyInMockedOrder();
    }
    // prepareForJobDoesNotInitializeIfSlotCanNotBeFound
    {
        Bench b;
        b.setHardwareId(kHardwareId);
        b.bus.when(C::getFeederAddress(kHardwareId)).timeout();
        assert(!b.prepareForJob(why) && why == "Failed to find and initialize the feeder");
        assert(!b.f().photonInitialized && !b.f().photonSlot);
        for (int i = 0; i <= b.config.photon().feederCommunicationMaxRetry(); ++i) b.bus.verify(C::getFeederAddress(kHardwareId));
        b.bus.verifyNothingSent();
    }
    // prepareForJobFindsFeederAgainIfLostToTimeout
    {
        Bench b;
        b.setHardwareId(kHardwareId);
        b.setOffset(kFeederOffset);
        b.setSlotAddress(kFeederAddress);
        b.bus.when(C::initializeFeeder(kFeederAddress, kHardwareId)).timeout();
        const int newAddress = 11;
        b.setSlotLocation(newAddress, kBaseLocation);
        JPPhotonPacket address = b.responses.getFeederAddressOk(newAddress), init = b.responses.initializeFeederOk(newAddress, kHardwareId);
        b.bus.when(C::getFeederAddress(kHardwareId)).reply(address);
        b.bus.when(C::initializeFeeder(newAddress, kHardwareId)).reply(init);
        assert(b.prepareForJob(why));
        assert(b.f().photonSlot == newAddress && b.f().photonInitialized);
        b.bus.verifyInMockedOrder();
    }
    // prepareForJobFindsFeederAgainIfWrongFeederUUIDAndMakesNewFeeder, ...AndUsesExistingFeeder
    for (const bool existing : { false, true }) {
        Bench b;
        b.setHardwareId(kHardwareId);
        b.setOffset(kFeederOffset);
        b.setSlotAddress(kFeederAddress);
        const std::string otherHardwareId = "445566778899AABBCCDDEEFF";
        std::string otherId;
        if (existing) {
            otherId = b.add();
            b.setHardwareId(otherId, otherHardwareId);
            assert(b.byHardwareId(otherHardwareId) == &b.f(otherId));
        } else {
            assert(!b.byHardwareId(otherHardwareId));
        }
        JPPhotonPacket wrong = b.responses.wrongFeederUUID(kFeederAddress, otherHardwareId);
        b.bus.when(C::initializeFeeder(kFeederAddress, kHardwareId)).reply(wrong);
        const int newAddress = 11;
        b.setSlotLocation(newAddress, kBaseLocation);
        JPPhotonPacket address = b.responses.getFeederAddressOk(newAddress), init = b.responses.initializeFeederOk(newAddress, kHardwareId);
        b.bus.when(C::getFeederAddress(kHardwareId)).reply(address);
        b.bus.when(C::initializeFeeder(newAddress, kHardwareId)).reply(init);
        assert(b.prepareForJob(why));
        assert(b.f().photonSlot == newAddress && b.f().photonInitialized);
        JPFeeder* other = b.byHardwareId(otherHardwareId);
        assert(other && (!existing || other->id() == otherId));
        assert(!other->photonInitialized && other->photonSlot == kFeederAddress);
        b.bus.verifyInMockedOrder();
    }
    // prepareForJobThrowsExceptionIfNewSlotHasNoLocation
    {
        Bench b;
        b.setHardwareId(kHardwareId);
        b.setSlotAddress(kFeederAddress);
        b.setSlotLocation(kFeederAddress, kBaseLocation);
        b.bus.when(C::initializeFeeder(kFeederAddress, kHardwareId)).timeout();
        const int newAddress = 11;
        JPPhotonPacket address = b.responses.getFeederAddressOk(newAddress), init = b.responses.initializeFeederOk(newAddress, kHardwareId);
        b.bus.when(C::getFeederAddress(kHardwareId)).reply(address);
        b.bus.when(C::initializeFeeder(newAddress, kHardwareId)).reply(init);
        assert(!b.prepareForJob(why) && why == "The slot at address 11 has no location configured.");   // UnconfiguredSlotException
        assert(b.f().photonInitialized && b.f().photonSlot == newAddress);
        b.bus.verifyInMockedOrder();
    }
    // prepareForJobThrowsExceptionIfFeederHasNoOffset
    {
        Bench b;
        b.setHardwareId(kHardwareId);
        b.setSlotAddress(kFeederAddress);
        b.setSlotLocation(kFeederAddress, kBaseLocation);
        JPPhotonPacket init = b.responses.initializeFeederOk(kFeederAddress, kHardwareId);
        b.bus.when(C::initializeFeeder(kFeederAddress, kHardwareId)).reply(init);
        assert(!b.prepareForJob(why) && why == "Photon Feeder with address " + kHardwareId + " has no location offset.");
        assert(b.f().photonInitialized);
        b.bus.verifyInMockedOrder();
    }
    // prepareForJobThrowsExceptionAfterOneRetry: the feeder's own id answering from further slots each time; were
    // it tried again once more, the bus would be asked about a slot not set up (NoPacketMocking).
    {
        Bench b;
        b.setHardwareId(kHardwareId);
        b.setSlotAddress(kFeederAddress);
        b.setMaxRetry(1);
        const int first = 11, second = 12;
        JPPhotonPacket toFirst = b.responses.wrongFeederUUID(first, kHardwareId), toSecond = b.responses.wrongFeederUUID(second, kHardwareId);
        b.bus.when(C::initializeFeeder(kFeederAddress, kHardwareId)).reply(toFirst);
        b.bus.when(C::initializeFeeder(first, kHardwareId)).reply(toSecond);
        assert(!b.prepareForJob(why) && why == "Failed to find and initialize the feeder");
        assert(b.f().photonSlot == second);
        b.bus.verifyInMockedOrder();
    }
    // prepareForJobThrowsExceptionAfterNoRetries
    {
        Bench b;
        b.setHardwareId(kHardwareId);
        b.setSlotAddress(kFeederAddress);
        b.setMaxRetry(0);
        const int newAddress = 11;
        JPPhotonPacket moved = b.responses.wrongFeederUUID(newAddress, kHardwareId);
        b.bus.when(C::initializeFeeder(kFeederAddress, kHardwareId)).reply(moved);
        assert(!b.prepareForJob(why) && why == "Failed to find and initialize the feeder");
        assert(b.f().photonSlot == newAddress);
        b.bus.verifyInMockedOrder();
    }
}

void getPartPitchByDefaultReturnsFourMillimeters() {
    Bench b;
    assert(b.f().number("part-pitch", 4) == 4);
}

// A feeder set up to feed: hardware id, pitch 2, offset, slot 5 with a location; not moving the nozzle unless asked.
void feedReady(Bench& b, bool moveWhileFeeding = false) {
    b.setHardwareId(kHardwareId);
    b.setPartPitch(2);
    b.setOffset(kFeederOffset);
    b.setSlotAddress(kFeederAddress);
    b.setMoveWhileFeeding(b.feeder, moveWhileFeeding);
    b.setSlotLocation(kFeederAddress, kBaseLocation);
}

void feed() {
    std::string why;
    // feedMovesPartForwardByPitch
    {
        Bench b;
        feedReady(b);
        JPPhotonPacket init = b.responses.initializeFeederOk(kFeederAddress, kHardwareId), moved = b.responses.moveFeedForwardOk(kFeederAddress, 0),
                       status = b.responses.moveFeedStatusOk(kFeederAddress);
        b.bus.when(C::initializeFeeder(kFeederAddress, kHardwareId)).reply(init);
        b.bus.when(C::moveFeedForward(kFeederAddress, 20)).reply(moved);
        b.bus.when(C::moveFeedStatus(kFeederAddress)).reply(status);
        assert(b.feed(why));
        b.bus.verifyInMockedOrder();
    }
    // feedInitializesIfUninitializedErrorIsReturned, feedInitializesOnUninitializedFeeder (another id in the reply)
    for (const std::string& replyId : { kHardwareId, std::string("FFEEDDCCBBAA998877665544") }) {
        Bench b;
        feedReady(b);
        JPPhotonPacket init = b.responses.initializeFeederOk(kFeederAddress, kHardwareId),
                       uninitialized = b.responses.uninitializedFeeder(kFeederAddress, replyId);
        b.bus.when(C::initializeFeeder(kFeederAddress, kHardwareId)).reply(init);
        b.bus.when(C::moveFeedForward(kFeederAddress, 20)).reply(uninitialized);
        const int newAddress = 11;
        b.setSlotLocation(newAddress, kBaseLocation);
        JPPhotonPacket address = b.responses.getFeederAddressOk(newAddress), init2 = b.responses.initializeFeederOk(newAddress, kHardwareId),
                       moved = b.responses.moveFeedForwardOk(newAddress, 0), status = b.responses.moveFeedStatusOk(newAddress);
        b.bus.when(C::getFeederAddress(kHardwareId)).reply(address);
        b.bus.when(C::initializeFeeder(newAddress, kHardwareId)).reply(init2);
        b.bus.when(C::moveFeedForward(newAddress, 20)).reply(moved);
        b.bus.when(C::moveFeedStatus(newAddress)).reply(status);
        assert(b.feed(why));
        b.bus.verifyInMockedOrder();
        assert(b.f().photonSlot == newAddress);
    }
    // feedThrowsExceptionAfterOneRetry
    {
        Bench b;
        feedReady(b, true);
        b.setMaxRetry(1);
        JPPhotonPacket init = b.responses.initializeFeederOk(kFeederAddress, kHardwareId), address = b.responses.getFeederAddressOk(kFeederAddress);
        b.bus.when(C::initializeFeeder(kFeederAddress, kHardwareId)).reply(init);
        b.bus.when(C::getFeederAddress(kHardwareId)).reply(address);
        b.bus.when(C::moveFeedForward(kFeederAddress, 20)).timeout();
        assert(!b.feed(why) && why == "Feed command timed out");   // FeedFailureException
        b.bus.verify(C::initializeFeeder(kFeederAddress, kHardwareId)).then(C::moveFeedForward(kFeederAddress, 20)).nothingElseSent();
        assert(!b.feed(why) && why == "Feed command timed out");
        b.bus.verify(C::getFeederAddress(kHardwareId))
            .then(C::initializeFeeder(kFeederAddress, kHardwareId))
            .then(C::moveFeedForward(kFeederAddress, 20))
            .nothingElseSent();
    }
    // feedThrowsExceptionAfterNoRetries, feedThrowsExceptionIfTheFeedTimesOut
    for (const int retries : { 0, 3 }) {
        Bench b;
        feedReady(b, true);
        b.setMaxRetry(retries);
        JPPhotonPacket init = b.responses.initializeFeederOk(kFeederAddress, kHardwareId);
        b.bus.when(C::initializeFeeder(kFeederAddress, kHardwareId)).reply(init);
        b.bus.when(C::moveFeedForward(kFeederAddress, 20)).timeout();
        assert(!b.feed(why) && why == "Feed command timed out");
        b.bus.verifyInMockedOrder();
    }
    // feedThrowsExceptionWhenFeederCannotBeInitialized
    {
        Bench b;
        b.setHardwareId(kHardwareId);
        b.setSlotAddress(kFeederAddress);
        b.setPartPitch(2);
        b.setMaxRetry(1);
        JPPhotonPacket address = b.responses.getFeederAddressOk(kFeederAddress);
        b.bus.when(C::initializeFeeder(kFeederAddress, kHardwareId)).timeout();
        b.bus.when(C::getFeederAddress(kHardwareId)).reply(address);
        assert(!b.feed(why) && why == "Failed to feed for an unknown reason. Is the feeder inserted?");
        b.bus.verify(C::initializeFeeder(kFeederAddress, kHardwareId))
            .then(C::getFeederAddress(kHardwareId))
            .then(C::initializeFeeder(kFeederAddress, kHardwareId))
            .nothingElseSent();
    }
    // feedWillCheckTheStatusAtLeastThreeTimesBeforeFailing
    {
        Bench b;
        feedReady(b);
        JPPhotonPacket init = b.responses.initializeFeederOk(kFeederAddress, kHardwareId), moved = b.responses.moveFeedForwardOk(kFeederAddress, 0);
        b.bus.when(C::initializeFeeder(kFeederAddress, kHardwareId)).reply(init);
        b.bus.when(C::moveFeedForward(kFeederAddress, 20)).reply(moved);
        b.bus.when(C::moveFeedStatus(kFeederAddress)).timeout();
        assert(!b.feed(why) && why == "Feeder timed out when we requested a feed status update.");
        auto& verification = b.bus.verify(C::initializeFeeder(kFeederAddress, kHardwareId)).then(C::moveFeedForward(kFeederAddress, 20));
        int count = 0;
        while (verification.hasMore()) {
            b.bus.verify(C::moveFeedStatus(kFeederAddress));
            ++count;
        }
        assert(count >= 3);
        verification.nothingElseSent();
    }
    // feedWillFailIfMotorCouldNotReachDestination
    {
        Bench b;
        feedReady(b);
        JPPhotonPacket init = b.responses.initializeFeederOk(kFeederAddress, kHardwareId), moved = b.responses.moveFeedForwardOk(kFeederAddress, 0),
                       fault = b.responses.couldNotReach(kFeederAddress);
        b.bus.when(C::initializeFeeder(kFeederAddress, kHardwareId)).reply(init);
        b.bus.when(C::moveFeedForward(kFeederAddress, 20)).reply(moved);
        b.bus.when(C::moveFeedStatus(kFeederAddress)).reply(fault);
        assert(!b.feed(why) && why == "Feeder could not reach its destination.");
        b.bus.verifyInMockedOrder();
    }
}

void twoFeedersCanNotHaveTheSameAddress() {
    Bench b;
    std::string why;
    b.config.removeFeeder(b.feeder);
    // Feeder A in slot 1, feeder B in slot 2.
    const std::string a = b.add(), bb = b.add();
    const std::string hwA = "445566778899AABBCCDDEEFF", hwB = "FFEEDDCCBBAA998877665544";
    for (const auto& [id, hw, slot] : { std::tuple { a, hwA, 1 }, std::tuple { bb, hwB, 2 } }) {
        b.setMoveWhileFeeding(id, false);
        b.setHardwareId(id, hw);
        b.setOffset(id, kFeederOffset);
        b.setSlotAddress(id, slot);
        b.setSlotLocation(slot, kBaseLocation);
    }
    b.f(bb).setNumber("part-pitch", 2);
    // Both initialized in their slots.
    JPPhotonPacket initA = b.responses.initializeFeederOk(1, kHardwareId), initB = b.responses.initializeFeederOk(2, hwB);
    b.bus.when(C::initializeFeeder(1, hwA)).reply(initA);
    b.bus.when(C::initializeFeeder(2, hwB)).reply(initB);
    assert(b.prepareForJob(a, why));
    b.bus.verify(C::initializeFeeder(1, hwA));
    assert(b.prepareForJob(bb, why));
    b.bus.verify(C::initializeFeeder(2, hwB));
    b.bus.verifyNothingSent();
    // A taken out and B put in slot 1: B's feed in slot 2 times out.
    b.bus.when(C::moveFeedForward(2, 20)).timeout();
    JPPhotonPacket foundB = b.responses.getFeederAddressOk(1), initB1 = b.responses.initializeFeederOk(1, hwB), moved = b.responses.moveFeedForwardOk(1, 0),
                   status = b.responses.moveFeedStatusOk(1);
    b.bus.when(C::getFeederAddress(hwB)).reply(foundB);
    b.bus.when(C::initializeFeeder(1, hwB)).reply(initB1);
    b.bus.when(C::moveFeedForward(1, 20)).reply(moved);
    b.bus.when(C::moveFeedStatus(1)).reply(status);
    assert(!b.feed(bb, why));
    b.bus.verify(C::moveFeedForward(2, 20)).nothingElseSent();   // not searched yet
    assert(b.feed(bb, why));
    b.bus.verify(C::getFeederAddress(hwB)).then(C::initializeFeeder(1, hwB)).then(C::moveFeedForward(1, 20)).then(C::moveFeedStatus(1)).nothingElseSent();
    assert(!b.f(a).photonInitialized && !b.f(a).photonSlot);
    assert(b.f(bb).photonInitialized && b.f(bb).photonSlot == 1);
}

void findSlotAddress() {
    // findSlotAddressForcesFind
    {
        Bench b;
        b.setHardwareId(kHardwareId);
        JPPhotonPacket first = b.responses.getFeederAddressOk(kFeederAddress), second = b.responses.getFeederAddressOk(11);
        b.bus.when(C::getFeederAddress(kHardwareId)).reply(first);
        b.findSlotAddress();
        assert(b.f().photonSlot == kFeederAddress);
        b.bus.when(C::getFeederAddress(kHardwareId)).reply(second);
        b.findSlotAddress();
        assert(b.f().photonSlot == 11);
        b.bus.verifyInMockedOrder();
    }
    // findSlotAddressClearsSlotAddressOnTimeout
    {
        Bench b;
        b.setHardwareId(kHardwareId);
        b.setSlotAddress(kFeederAddress);
        b.bus.when(C::getFeederAddress(kHardwareId)).timeout();
        b.findSlotAddress();
        assert(!b.f().photonSlot);
        b.bus.verifyInMockedOrder();
    }
}

void findAllFeeders() {
    // findAllFeedersUsingMaxFeederAddress
    {
        Bench b;
        const int most = 5;
        b.config.photon().setMaxFeederAddress(most);
        std::map<int, std::string> uuids;
        std::vector<JPPhotonPacket> replies;
        replies.reserve(most);
        for (int address = 1; address <= most; ++address) {
            uuids[address] = b.randomUUID();
            replies.push_back(b.responses.getFeederIdOk(address, uuids[address]));
            b.bus.when(C::getFeederId(address)).reply(replies.back());
        }
        b.findAllFeeders();
        for (int address = 1; address <= most; ++address) {
            JPFeeder* byUuid = b.byHardwareId(uuids[address]);
            JPFeeder* byAddress = b.bySlotAddress(address);
            assert(byUuid && byAddress && byUuid->id() == byAddress->id());
            assert(byUuid->photonSlot == address && byAddress->text("hardware-id") == uuids[address]);
        }
        b.bus.verifyInMockedOrder();
    }
    // findAllFeedersFindsNewAndExistingFeeders: the known one answering at 2, a new one at 1, nothing at 3 to 5
    {
        Bench b;
        b.config.photon().setMaxFeederAddress(5);
        const std::string newUuid = "FFEEDDCCBBAA998877665544";
        b.f().setName(kHardwareId);
        b.setHardwareId(kHardwareId);
        b.setSlotAddress(1);
        JPPhotonPacket one = b.responses.getFeederIdOk(1, newUuid), two = b.responses.getFeederIdOk(2, kHardwareId);
        b.bus.when(C::getFeederId(1)).reply(one);
        b.bus.when(C::getFeederId(2)).reply(two);
        for (int i = 3; i <= 5; ++i) b.bus.when(C::getFeederId(i)).timeout();
        b.findAllFeeders();
        assert(b.f().photonSlot == 2 && b.f().name() == kHardwareId + " (Slot: 2)");
        JPFeeder* made = b.byHardwareId(newUuid);
        assert(made && made->photonSlot == 1 && made->text("hardware-id") == newUuid && made->name() == newUuid + " (Slot: 1)");
        b.bus.verifyInMockedOrder();
    }
    // findAllFeedersFillsNullHardwareIdFeedersBeforeCreatingNewOnes
    {
        Bench b;
        b.config.photon().setMaxFeederAddress(2);
        const std::string newUuid = "FFEEDDCCBBAA998877665544";
        JPPhotonPacket one = b.responses.getFeederIdOk(1, kHardwareId), two = b.responses.getFeederIdOk(2, newUuid);
        b.bus.when(C::getFeederId(1)).reply(one);
        b.bus.when(C::getFeederId(2)).reply(two);
        b.findAllFeeders();
        assert(b.f().photonSlot == 1 && b.f().text("hardware-id") == kHardwareId && b.f().name() == kHardwareId + " (Slot: 1)");
        JPFeeder* made = b.byHardwareId(newUuid);
        assert(made && made->photonSlot == 2 && made->text("hardware-id") == newUuid && made->name() == newUuid + " (Slot: 2)");
        b.bus.verifyInMockedOrder();
    }
    // findAllFeedersRemovesFeederAddressIfTimeoutOccurs
    {
        Bench b;
        b.config.photon().setMaxFeederAddress(5);
        b.setHardwareId(kHardwareId);
        b.setSlotAddress(1);
        for (int i = 1; i <= 5; ++i) b.bus.when(C::getFeederId(i)).timeout();
        b.findAllFeeders();
        assert(!b.f().photonSlot);
        b.bus.verifyInMockedOrder();
    }
}

void propertySheets() {
    auto form = [](Bench& b) { return JPFeederForms::forFeeder(b.config, b.feeder, nullptr, {}); };
    // getPropertySheetHolderTitleDefault, getPropertySheetHolderTitleUsesHardwareIdNameIfConfigured
    {
        Bench b;
        assert(form(b).title == "Unconfigured PhotonFeeder");
        b.setHardwareId(kHardwareId);
        b.setSlotAddress(15);
        assert(form(b).title == "PhotonFeeder " + b.f().name());
    }
    // getPropertySheetsOnlyReturnsGlobalConfigWithNullHardwareId, getPropertySheetsAlsoReturnsFeederConfigurationWithHardwareIdSet
    {
        Bench b;
        const auto bare = form(b);
        assert(bare.tabs.size() == 1 && bare.tabs[0].title == "Global Config");
        b.setHardwareId(kHardwareId);
        const auto both = form(b);
        assert(both.tabs.size() == 2 && both.tabs[0].title == "Feeder" && both.tabs[1].title == "Global Config");
    }
}

void findIssues() {
    // findIssuesGivesNothingIfNoHardwareIdIsPresent
    {
        Bench b;
        b.setSlotAddress(kFeederAddress);
        assert(!b.issueAbout(b.feeder));
    }
    // findIssuesAddsIssueIfSlotHasNoLocationSet
    {
        Bench b;
        b.setHardwareId(kHardwareId);
        b.setOffset(kFeederOffset);
        b.setSlotAddress(kFeederAddress);
        assert(!b.f().photonSlotLocation);
        const JPSolutions::Issue* i = b.issueAbout(b.feeder);
        assert(i && i->issue == "Feeder slot has no configured location" && i->severity == JPSolutions::Severity::Error
               && i->state == JPSolutions::State::Open);
    }
    // findIssuesAddsIssueIfFeederHasNoOffsetSet
    {
        Bench b;
        b.setHardwareId(kHardwareId);
        b.setSlotAddress(kFeederAddress);
        b.setSlotLocation(kFeederAddress, kBaseLocation);
        const JPSolutions::Issue* i = b.issueAbout(b.feeder);
        assert(i && i->issue == "Feeder has no configured offset" && i->severity == JPSolutions::Severity::Error
               && i->state == JPSolutions::State::Open);
    }
}

void pickLocation() {
    // getPickLocationThrowsExceptionIfNoSlotAddressIsSet, ...IfSlotHasNoLocation, ...IfFeederHasNoOffset: none, and why
    Bench b;
    assert(!b.f().pickLocation() && b.f().photonUnconfigured() == "Photon Feeder with address  has no address. Is it inserted?");
    b.setSlotAddress(kFeederAddress);
    assert(!b.f().pickLocation() && b.f().photonUnconfigured() == "The slot at address 5 has no location configured.");
    b.setSlotLocation(kFeederAddress, kBaseLocation);
    assert(!b.f().pickLocation() && b.f().photonUnconfigured() == "Photon Feeder with address  has no location offset.");
    // getPickLocationUsesOffsetWithRotation
    b.setOffset(kFeederOffset);
    const auto at = b.f().pickLocation();
    assert(at && *at == JPLocation(JPLengthUnit::Millimeters, 2, 3, 3, 45));
}

void slotsAndProperties() {
    Bench b;
    // PhotonFeederSlotsTest.byDefaultAnUnknownSlotHasNoLocationConfigured
    assert(!b.config.photon().slotLocation(5));
    // PhotonPropertiesTest.getFeederSlotsCausesFeederSlotsToBeSetOnMachine: the slots kept with the machine's
    // properties (written and read back as OpenPnP's PhotonFeeder.FeederSlots)
    b.setSlotLocation(5, kBaseLocation);
    JPPhotonProperties read;
    for (const JPXmlNode& n : b.config.photon().toXml()) {
        JPXmlElement e;
        std::string error;
        assert(JPXmlReader::parse(JPXmlWriter::text(n), e, error));
        read.take(e);
    }
    assert(read.slotLocation(5) == kBaseLocation);
    // PhotonPropertiesTest.byDefaultTheMaxFeederAddressIs50
    assert(JPPhotonProperties().maxFeederAddress() == 50);
}

void loading() {
    // PhotonFeederLoadingTest.loadingOfPhotonProperties: a configuration has its machine's Photon settings
    Bench b;
    assert(b.config.photon().feederCommunicationMaxRetry() == JPPhotonProperties::kDefaultMaxRetry);
    // loadingOfDataActuator
    JPCellConfig cell;
    assert(JPPhotonFeeders::addDataActuator(cell) && cell.actuatorNamed(JPPhotonBus::kDataActuator));
    // loadingOfDataActuatorFillsInGcodeForGcodeDrivers
    JPCellConfig gcode;
    JPDriverConfig d;
    d.id = "D";
    gcode.drivers.push_back(d);
    assert(JPPhotonFeeders::addDataActuator(gcode));
    const JPActuatorConfig* a = gcode.actuatorNamed(JPPhotonBus::kDataActuator);
    assert(a && a->driverId == "D" && a->readCommand == "M485 {value}" && a->readPattern == "rs485-reply: (?<Value>.*)");
}

} // namespace

int main() {
    getJobPreparationLocationReturnsNull();
    getDataActuatorCreatesReferenceActuatorIfOneDoesNotExist();
    getSlotAddressReturnsNullByDefault();
    isEnabled();
    names();
    isInitializedByDefaultReturnsFalse();
    prepareForJob();
    getPartPitchByDefaultReturnsFourMillimeters();
    feed();
    twoFeedersCanNotHaveTheSameAddress();
    findSlotAddress();
    findAllFeeders();
    propertySheets();
    findIssues();
    pickLocation();
    slotsAndProperties();
    loading();
    return 0;
}
