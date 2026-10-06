// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPPhotonFeeders.h"

#include "JPPhotonBus.h"
#include "JPPhotonCommands.h"

#include "common/JPlacerLog.h"

#include <j/core/Log.h>

#include <chrono>
#include <thread>

inline namespace jf {

namespace {

constexpr const char* kPhotonClass = "org.openpnp.machine.photon.PhotonFeeder";
// Between asks of a feed's status (OpenPnP's).
constexpr auto kStatusWait = std::chrono::milliseconds(50);
// A feed is given this many times the time it says it takes.
constexpr int kFeedTimeFactor = 3;

using Error = JPPhotonCommands::Error;

struct Run {
    JPConfiguration&          config;
    JPJobMachine&             machine;
    const JPPhotonFeeders::OnMain& onMain;
    void main(const std::function<void()>& fn) const {
        if (onMain) onMain(fn);
        else fn();
    }
};

// A Photon feeder's hardware id given: one still named as its class is named by it (OpenPnP's setHardwareId).
void setHardwareId(JPFeeder& f, const std::string& id) {
    if (f.text("name") == f.typeName()) f.setText("name", id);
    f.setText("hardware-id", id);
}

bool findSlotAddress(const Run& r, const std::string& feederId, bool force, std::string& why) {
    std::string id;
    bool known = false;
    r.main([&] {
        if (const JPFeeder* f = r.config.feeder(feederId)) {
            id = f->text("hardware-id");
            known = f->photonSlot.has_value();
        }
    });
    if (known && !force) return true;
    std::string seen;
    const auto got = JPPhotonCommands::send(JPPhotonFeeders::bus(r.machine), JPPhotonCommands::getFeederAddress(id), seen);
    if (!seen.empty() && !got) {
        why = seen;
        return false;
    }
    r.main([&] { r.config.setPhotonSlot(feederId, got ? std::optional(got->fromAddress) : std::nullopt); });
    return true;
}

bool initializeIfNeeded(const Run& r, const std::string& feederId, std::string& why) {
    std::string id;
    std::optional<int> slot;
    bool done = false;
    r.main([&] {
        if (const JPFeeder* f = r.config.feeder(feederId)) {
            id = f->text("hardware-id");
            slot = f->photonSlot;
            done = f->photonInitialized;
        }
    });
    if (done || !slot) return true;
    std::string seen;
    const auto got = JPPhotonCommands::send(JPPhotonFeeders::bus(r.machine), JPPhotonCommands::initializeFeeder(*slot, id), seen);
    if (!seen.empty() && !got) {
        why = seen;
        return false;
    }
    r.main([&] {
        if (!got) {
            if (JPFeeder* f = r.config.feeder(feederId)) f->photonSlot.reset();
            r.config.resolvePhoton();
        } else if (got->error == Error::WrongFeederUuid) {
            // Another feeder answers there: that one is at this address.
            const std::string other = got->uuid.value_or("");
            if (!r.config.photonFeeder(other)) {
                JPFeeder made = JPFeeder::create(kPhotonClass, "");
                setHardwareId(made, other);
                r.config.addFeeder(std::move(made));
            }
            r.config.setPhotonSlot(r.config.photonFeeder(other)->id(), got->fromAddress);
        } else if (JPFeeder* f = r.config.feeder(feederId)) {
            f->photonInitialized = true;
        }
    });
    return true;
}

int maxRetry(const Run& r) {
    int n = 0;
    r.main([&] { n = r.config.photon().feederCommunicationMaxRetry(); });
    return n;
}

} // namespace

JPPhotonBusInterface& JPPhotonFeeders::bus(JPJobMachine& machine) {
    // OpenPnP's one bus (from address 0): its packets numbered in turn, whichever feeder sends them.
    static JPJobMachine* s_machine = nullptr;
    static JPPhotonBus s_bus(0, [](const std::string& parameter, std::string& value, std::string& why) {
        return s_machine->readActuator(JPPhotonBus::kDataActuator, parameter, value, why);
    });
    s_machine = &machine;
    return s_bus;
}

bool JPPhotonFeeders::findSlotAddress(JPConfiguration& config, const std::string& feederId, JPJobMachine& machine,
                                      const OnMain& onMain, std::string& why) {
    return jf::findSlotAddress(Run { config, machine, onMain }, feederId, true, why);
}

bool JPPhotonFeeders::feed(JPConfiguration& config, const std::string& feederId, const std::string& nozzleId, int distanceMm,
                           JPJobMachine& machine, const OnMain& onMain, std::string& why) {
    const Run r { config, machine, onMain };
    const int retries = maxRetry(r);
    for (int i = 0; i <= retries; ++i) {
        if (!jf::findSlotAddress(r, feederId, false, why) || !initializeIfNeeded(r, feederId, why)) return false;
        bool ready = false, moveWhile = true;
        std::string unconfigured;
        std::optional<int> slot;
        std::optional<JPLocation> pick;
        r.main([&] {
            if (const JPFeeder* f = config.feeder(feederId)) {
                ready = f->photonInitialized;
                unconfigured = f->photonUnconfigured();
                slot = f->photonSlot;
                pick = f->pickLocation();
                moveWhile = f->flag("move-while-feeding", true);
            }
        });
        if (!ready) continue;
        if (!unconfigured.empty()) {
            why = unconfigured;
            return false;
        }
        JPPhotonBusInterface& bus = JPPhotonFeeders::bus(machine);
        std::string seen;
        const auto moved = JPPhotonCommands::send(bus, JPPhotonCommands::moveFeedForward(*slot, distanceMm * 10), seen);
        if (!moved) {
            r.main([&] {
                if (JPFeeder* f = config.feeder(feederId)) {
                    f->photonSlot.reset();
                    f->photonInitialized = false;
                }
            });
            why = seen.empty() ? "Feed command timed out" : seen;
            return false;
        }
        if (moved->error == Error::UninitializedFeeder) {
            r.main([&] {
                if (JPFeeder* f = config.feeder(feederId)) {
                    f->photonSlot.reset();
                    f->photonInitialized = false;
                }
            });
            continue;   // initialized again on the next try
        }
        const auto end = std::chrono::steady_clock::now() + kFeedTimeFactor * std::chrono::milliseconds(moved->expectedTimeToFeed);
        for (int j = 0; j <= retries || std::chrono::steady_clock::now() <= end; ++j) {
            std::this_thread::sleep_for(kStatusWait);
            // The nozzle over the pick (at safe Z) while it feeds.
            if (j == 0 && !nozzleId.empty() && pick && moveWhile && machine.isHomed()) {
                std::string moveWhy;
                if (!machine.positionNozzle(nozzleId, *pick, moveWhy)) {
                    why = moveWhy;
                    return false;
                }
            }
            const auto status = JPPhotonCommands::send(bus, JPPhotonCommands::moveFeedStatus(*slot), seen);
            if (!status) continue;   // no answer: asked again after a while
            if (status->error == Error::None) return true;
            if (status->error == Error::CouldNotReach) {
                why = "Feeder could not reach its destination.";
                return false;
            }
        }
        why = "Feeder timed out when we requested a feed status update.";
        return false;
    }
    why = "Failed to feed for an unknown reason. Is the feeder inserted?";
    return false;
}

bool JPPhotonFeeders::prepareForJob(JPConfiguration& config, const std::string& feederId, JPJobMachine& machine,
                                    const OnMain& onMain, std::string& why) {
    const Run r { config, machine, onMain };
    const int retries = maxRetry(r);
    for (int i = 0; i <= retries; ++i) {
        if (!jf::findSlotAddress(r, feederId, false, why) || !initializeIfNeeded(r, feederId, why)) return false;
        bool ready = false;
        std::string unconfigured;
        r.main([&] {
            if (const JPFeeder* f = config.feeder(feederId)) {
                ready = f->photonInitialized;
                unconfigured = f->photonUnconfigured();
            }
        });
        if (!ready) continue;
        why = unconfigured;
        return unconfigured.empty();
    }
    why = "Failed to find and initialize the feeder";
    return false;
}

bool JPPhotonFeeders::findAll(JPConfiguration& config, JPJobMachine& machine, const OnMain& onMain,
                              const std::function<void(int address, SearchState state)>& progress, std::string& why) {
    const Run r { config, machine, onMain };
    int most = 0;
    r.main([&] { most = config.photon().maxFeederAddress(); });
    JLOGC(JPlacerLog::kJob, JLogLevel::Info) << "Searching for Photon Feeders";
    std::vector<JPFeeder> toAdd;
    JPPhotonBusInterface& bus = JPPhotonFeeders::bus(machine);
    for (int address = 1; address <= most; ++address) {
        if (progress) progress(address, SearchState::Searching);
        std::string seen;
        const auto got = JPPhotonCommands::send(bus, JPPhotonCommands::getFeederId(address), seen);
        if (!got && !seen.empty()) {
            why = seen;
            return false;
        }
        if (progress) progress(address, got ? SearchState::Found : SearchState::Missing);
        r.main([&] {
            if (!got) {
                for (JPFeeder& f : config.feeders())
                    if (f.isPhoton() && f.photonSlot == address) {
                        f.photonSlot.reset();
                        f.photonInitialized = false;
                    }
                return;
            }
            const std::string uuid = got->uuid.value_or("");
            JPFeeder* f = config.photonFeeder(uuid);
            if (!f) f = config.photonFeeder("");
            if (!f)
                for (JPFeeder& added : toAdd)
                    if (added.text("hardware-id") == uuid) f = &added;
            if (!f) {
                toAdd.push_back(JPFeeder::create(kPhotonClass, ""));
                f = &toAdd.back();
            }
            setHardwareId(*f, uuid);
            if (config.feeder(f->id())) config.setPhotonSlot(f->id(), address);
            else f->photonSlot = address;
        });
    }
    r.main([&] {
        for (JPFeeder& f : toAdd) {
            const std::optional<int> slot = f.photonSlot;
            const std::string id = f.id();
            config.addFeeder(std::move(f));
            config.setPhotonSlot(id, slot);
        }
    });
    return true;
}

} // inline namespace jf
