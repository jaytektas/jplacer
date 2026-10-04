// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "model/JPLocation.h"

#include <optional>
#include <string>
#include <vector>

inline namespace jf {

// What a job (JPJobProcessor) asks of the machine, each call made on the
// job's own thread and waiting until it is done: false, with why, when it
// could not be. Places are in the machine's coordinates, a nozzle's place
// being where its tip touches (the part's angle as its rotation); a camera's
// is where it looks.
class JPJobMachine {
public:
    struct Nozzle {
        std::string              id, name;
        std::string              tipId;    // on it now; empty: none
        std::vector<std::string> tipIds;   // the tips that fit it
    };

    virtual ~JPJobMachine() = default;

    // The head's nozzles, in order, and the machine's nozzle tips (id, name).
    virtual std::vector<Nozzle> nozzles() const = 0;
    virtual std::vector<std::pair<std::string, std::string>> tips() const = 0;
    // Where the head's camera is now; none when it cannot be told.
    virtual std::optional<JPLocation> cameraLocation() const = 0;

    // Every nozzle up into its safe zone.
    virtual bool safeZ(std::string& why) = 0;
    // The tip on `nozzleId` changed for `tipId`: the one on it unloaded, then `tipId` loaded.
    virtual bool changeTip(const std::string& nozzleId, const std::string& tipId, std::string& why) = 0;
    // Turn the nozzle to `angle` where it is (OpenPnP's pre-rotation); a nozzle without a rotation axis stays.
    virtual bool rotate(const std::string& nozzleId, double angle, std::string& why) = 0;
    // Up to safe Z, across and turned to `at`, down to its Z, the part picked
    // (the vacuum on, the dwell, the part checked as the tip says), and up.
    virtual bool pick(const std::string& nozzleId, const JPLocation& at, std::string& why) = 0;
    // The same to `at`, the part let go there (and checked gone).
    virtual bool place(const std::string& nozzleId, const JPLocation& at, std::string& why) = 0;
    // What the nozzle holds dropped at the discard location.
    virtual bool discard(const std::string& nozzleId, std::string& why) = 0;
    // The head to its park place.
    virtual bool park(std::string& why) = 0;
    // The camera over `nominal` (at safe Z), a round mark of `diameterMm`
    // found near there, and where it is: `found`.
    virtual bool locateFiducial(const JPLocation& nominal, double diameterMm, JPLocation& found, std::string& why) = 0;
};

} // inline namespace jf
