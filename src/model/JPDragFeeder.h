// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPPinFeeder.h"

inline namespace jf {

// A drag feeder (OpenPnP's ReferenceDragFeeder): its pin dropped into the tape and dragged from its feed start to
// its feed end, then backed off its backoff distance to take the tension off. What a pin feeder has, and that.
class JPDragFeeder : public JPPinFeeder {
public:
    static constexpr const char* kClass = "org.openpnp.machine.reference.feeder.ReferenceDragFeeder";
    static constexpr const char* kKey = "drag";

    JPLength backoffDistance { 0, JPLengthUnit::Millimeters };

    // OpenPnP's settings of it (its <feeder>'s own, what every feeder has taken out).
    static JPDragFeeder fromXml(const JPXmlNode& own);
    // As jplacer's feeders.json keeps it: a pin feeder's, and {"backoffDistanceMm"}.
    static JPDragFeeder fromJson(const JJson& j);
    JJson toJson() const;
};

} // inline namespace jf
