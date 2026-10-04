// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPJobMachine.h"

#include "model/JPConfiguration.h"

#include <functional>
#include <string>
#include <vector>

inline namespace jf {

// Where boards and panels really lie, from their fiducials, as OpenPnP's
// ReferenceFiducialLocator: each one's fiducials (its placements of type
// Fiducial on the side facing up, enabled; a panel's alignment
// pseudo-placements too; two at least) looked at with the head's camera in
// a short way round, each found as a round mark the size of its footprint's
// pad (JPJobMachine::locateFiducial), then where they should be fitted onto
// where they were found (JPFiducialFit) and the board or panel set by that.
// A fit that scales, shears or moves the board more than the tolerances
// allow is refused, the board left where it was. Runs on a thread of its
// own; the model is touched only through `onMain`.
class JPFiducialLocator {
public:
    // OpenPnP's FiducialLocatorTolerances, with its defaults.
    struct Tolerances {
        double scaling = 0.05, shearing = 0.05, boardLocationMm = 5.0;
        // The machine's fiducial vision settings: what a fiducial's part and package do not name.
        std::string fiducialVisionId = "FVS_Default";
    };
    struct Result {
        bool        ok = false;
        std::string message;            // when not
        // What it is about: a board or panel (its unique id), a fiducial
        // placement on it, or a part.
        enum class About { Board, Placement, Part } about = About::Board;
        std::string id, placementId;
        JPLocation  location { JPLengthUnit::Millimeters };   // the last one's, where its fiducials put it
    };
    using OnMain = std::function<void(const std::function<void()>&)>;

    static Result locate(JPConfiguration& config, JPJobMachine& machine, const OnMain& onMain,
                         const std::vector<JPPlacementsHolderLocation*>& locations, const Tolerances& tolerances);
};

} // inline namespace jf
