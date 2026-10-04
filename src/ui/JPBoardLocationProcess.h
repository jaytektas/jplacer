// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "model/JPPlacementsHolderLocation.h"

#include <functional>
#include <optional>
#include <string>
#include <vector>

inline namespace jf {

// The Job tab's Multiple Point Board Location, as OpenPnP's
// MultiPlacementBoardLocationProcess: choose two or more placements (four,
// near the corners, is better); the camera is taken near each in turn (the
// shortest way, ending towards the board's origin) and jogged over its
// centre, Next taking where it is; then the board is fitted to them as a
// fiducial check fits it (JPFiducialFit), refused when it scales, shears or
// moves more than 5 %, 5 % or 5 mm. Finish takes the camera to the board's
// origin; Cancel puts the board back as it was.
class JPBoardLocationProcess {
public:
    enum class Tool { Camera, Nozzle };
    struct Hooks {
        std::function<std::optional<JPLocation>(Tool)>         toolLocation;
        std::function<void(Tool, const JPLocation&)>           moveTool;
        std::function<std::vector<JPPlacement*>()>             chosenPlacements;
        std::function<void(const std::string& id)>             selectPlacement;
        // A step's instructions shown: title, text, the proceed button's label, Cancel, proceed.
        std::function<void(const std::string&, const std::string&, const std::string&, std::function<void()>,
                           std::function<void()>)>
                              show;
        std::function<void()> finished;   // done or cancelled: the instructions gone
    };

    // `topLevel`: the board lies straight in the job (its location is set too).
    JPBoardLocationProcess(JPPlacementsHolderLocation& location, bool topLevel, Hooks hooks);

private:
    void advance();
    bool step1();
    bool step2();
    void cancel();
    void showStep();
    // The fit made and set; with `check`, refused (false, said) outside the tolerances.
    bool setLocation(bool check);
    std::vector<JPPlacement> shortestOrder(std::vector<JPPlacement> placements) const;
    void moveCameraTo(const JPPlacement& p);

    JPPlacementsHolderLocation&        m_location;
    bool                               m_topLevel;
    Hooks                              m_hooks;
    int                                m_step = -1;
    std::vector<JPPlacement>           m_placements;
    std::vector<JPLocation>            m_expected, m_measured;
    size_t                             m_index = 0;
    std::string                        m_placementId;
    JPSide                             m_side;
    JPLocation                         m_savedLocation;
    std::optional<JPAffineTransform>   m_savedTransform;
};

} // inline namespace jf
