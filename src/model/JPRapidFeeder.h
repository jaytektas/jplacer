// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPLength.h"
#include "JPLocation.h"

#include "openpnp/JPXmlNode.h"

#include <j/config/Json.h>

#include <string>

inline namespace jf {

// A Rapid feeder (OpenPnP's RapidFeeder): fed by sending its address and pitch to the RAPIDFEEDER actuator. Its
// address (from the QR code a scan finds), its pitch in whole millimetres, and the line a scan for the feeders' QR
// codes runs along, in steps of its increment.
class JPRapidFeeder {
public:
    static constexpr const char* kClass = "org.openpnp.machine.rapidplacer.RapidFeeder";
    static constexpr const char* kKey = "rapid";

    std::string address;
    int         pitch = 4;
    JPLocation  scanStart { JPLengthUnit::Millimeters }, scanEnd { JPLengthUnit::Millimeters };
    JPLength    scanIncrement { 4, JPLengthUnit::Millimeters };

    // OpenPnP's settings of it (its <feeder>'s own, what every feeder has taken out).
    static JPRapidFeeder fromXml(const JPXmlNode& own);
    // As jplacer's feeders.json keeps it: {"address", "pitch", "scanStart", "scanEnd", "scanIncrementMm"}.
    static JPRapidFeeder fromJson(const JJson& j);
    JJson toJson() const;
};

} // inline namespace jf
