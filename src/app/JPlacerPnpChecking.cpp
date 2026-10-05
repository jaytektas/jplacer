// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPlacerPnpChecking.h"

#include "machine/JPCameraConfig.h"

inline namespace jf {

namespace {
// OpenPnP's ImageCamera defaults: how far off a pick and a place may be, and how well each must match.
constexpr double kPickToleranceMm = 0.1, kPickMinimumScore = 0.87;
constexpr double kPlaceToleranceMm = 0.1, kPlaceMinimumScore = 0.58;
} // namespace

void JPlacerPnpChecking::hold(const std::string& nozzleId, std::shared_ptr<const JPFootprint> footprint) {
    std::lock_guard lk(m_state->mutex);
    if (footprint) m_state->footprints[nozzleId] = std::move(footprint);
    else m_state->footprints.erase(nozzleId);
}

JPCell::PnpChecker JPlacerPnpChecking::checker() {
    return [state = m_state](const JPCell::PnpCheck& c, std::string& detail) {
        std::shared_ptr<const JPFootprint> footprint;
        {
            std::lock_guard lk(state->mutex);
            if (const auto it = state->footprints.find(c.nozzleId); it != state->footprints.end()) footprint = it->second;
        }
        if (!footprint) {
            detail = "part " + c.partId + " has no footprint";
            return false;
        }
        const JJson& d = c.camera;
        JPSimulatedPnpCheck::Picture picture;
        picture.path = d["source"].str();
        picture.unitsPerPixelX = d["imageUnitsPerPixel"]["x"].number(JPCameraConfig::kDefaultImageUnitsPerPixel);
        picture.unitsPerPixelY = d["imageUnitsPerPixel"]["y"].number(JPCameraConfig::kDefaultImageUnitsPerPixel);
        picture.offsetX = d["imageOffset"]["x"].number(0);
        picture.offsetY = d["imageOffset"]["y"].number(0);
        picture.filterTestImage = d["filterTestImageVision"].boolean(true);
        const JPSimulatedPnpCheck::Tolerance tolerance =
            c.pick ? JPSimulatedPnpCheck::Tolerance { d["pickLocationToleranceMm"].number(kPickToleranceMm),
                                                      d["pickLocationMinimumScore"].number(kPickMinimumScore) }
                   : JPSimulatedPnpCheck::Tolerance { d["placeLocationToleranceMm"].number(kPlaceToleranceMm),
                                                      d["placeLocationMinimumScore"].number(kPlaceMinimumScore) };
        std::lock_guard lk(state->mutex);   // one check at a time (the engine keeps the pictures)
        return state->engine.isPartLocation(picture, *footprint, c.x, c.y, c.rotation, c.pick, tolerance, detail);
    };
}

} // inline namespace jf
