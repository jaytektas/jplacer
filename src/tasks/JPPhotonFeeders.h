// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPJobMachine.h"
#include "JPPhotonBusInterface.h"

#include "machine/JPCellConfig.h"
#include "model/JPConfiguration.h"

#include <functional>
#include <string>

inline namespace jf {

// The Photon feeders on the bus (JPPhotonBus), as OpenPnP's PhotonFeeder
// does it: a feeder found by its hardware id (its slot address asked of
// every feeder), set up at that address (a feeder of another id answering
// there is taken as that one, made when unknown), fed (forward by its part
// pitch, the nozzle taken over its pick while it feeds, its status asked
// until it is done), each tried again as the machine's Photon settings say.
// On a thread of its own; the model touched only through `onMain`.
class JPPhotonFeeders {
public:
    using OnMain = std::function<void(const std::function<void()>&)>;
    // OpenPnP's FeederSearchState.
    enum class SearchState { Unknown, Searching, Found, Missing };

    // OpenPnP's PhotonFeeder.getBus: the one bus every Photon feeder talks on, through `machine`'s
    // PhotonFeederData actuator.
    static JPPhotonBusInterface& bus(JPJobMachine& machine);
    // OpenPnP's setBus: the bus used in its place (null: the machine's again).
    static void setBus(JPPhotonBusInterface* bus);
    // OpenPnP's getDataActuator: the PhotonFeederData actuator added to the machine when it has none, read by
    // "M485 {value}" through its first G-code controller, the reply's "rs485-reply: (?<Value>.*)". False: it had one.
    static bool addDataActuator(JPCellConfig& cell);

    // Find: its slot address asked afresh (none when it does not answer).
    static bool findSlotAddress(JPConfiguration& config, const std::string& feederId, JPJobMachine& machine,
                                const OnMain& onMain, std::string& why);
    // A feed of `distanceMm` (its part pitch, or 1 for Feed 1mm), with the
    // nozzle (none: not moved) taken over its pick while it feeds.
    static bool feed(JPConfiguration& config, const std::string& feederId, const std::string& nozzleId, int distanceMm,
                     JPJobMachine& machine, const OnMain& onMain, std::string& why);
    // Before a job: found and set up, else "Failed to find and initialize the feeder".
    static bool prepareForJob(JPConfiguration& config, const std::string& feederId, JPJobMachine& machine,
                              const OnMain& onMain, std::string& why);
    // Every address up to the machine's highest asked for its feeder: a
    // feeder found is the one of its id (else the first not yet given one,
    // else a new one), at that address; one not answering loses its address.
    static bool findAll(JPConfiguration& config, JPJobMachine& machine, const OnMain& onMain,
                        const std::function<void(int address, SearchState state)>& progress, std::string& why);
};

} // inline namespace jf
