// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPLocation.h"

#include "openpnp/JPXmlNode.h"

#include <j/config/Json.h>

#include <optional>
#include <string>

inline namespace jf {

// What a Photon feeder (OpenPnP's PhotonFeeder) has of its own: it is known on the bus by its hardware id (none
// yet: unconfigured), feeds a part pitch in whole millimetres, picks at its offset from the slot it is found in
// (none: not set up yet), and may move to the pick while it feeds.
class JPPhotonFeeder {
public:
    static constexpr const char* kClass = "org.openpnp.machine.photon.PhotonFeeder";
    static constexpr const char* kKey = "photon";

    std::string               hardwareId;
    int                       partPitch = 4;
    std::optional<JPLocation> offset;
    bool                      moveWhileFeeding = true;

    // OpenPnP's settings of it (its <feeder>'s own, what every feeder has taken out).
    static JPPhotonFeeder fromXml(const JPXmlNode& own);
    // As jplacer's feeders.json keeps it: {"hardwareId", "partPitch", "offset" (none: not set up), "moveWhileFeeding"}.
    static JPPhotonFeeder fromJson(const JJson& j);
    JJson toJson() const;
};

} // inline namespace jf
