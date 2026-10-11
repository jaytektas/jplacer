// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPPinFeeder.h"

inline namespace jf {

// A lever feeder (OpenPnP's ReferenceLeverFeeder): its pin pushes a lever from its feed start to its feed end and
// lets it back, once for each 4 mm of the part pitch. What a pin feeder has.
class JPLeverFeeder : public JPPinFeeder {
public:
    static constexpr const char* kClass = "org.openpnp.machine.reference.feeder.ReferenceLeverFeeder";
    static constexpr const char* kKey = "lever";

    // OpenPnP's settings of it (its <feeder>'s own, what every feeder has taken out).
    static JPLeverFeeder fromXml(const JPXmlNode& own);
    // As jplacer's feeders.json keeps it: a pin feeder's.
    static JPLeverFeeder fromJson(const JJson& j);
    JJson toJson() const { return json(); }
};

} // inline namespace jf
