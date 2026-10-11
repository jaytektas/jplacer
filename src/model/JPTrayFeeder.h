// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPLocation.h"

#include "openpnp/JPXmlNode.h"

#include <j/config/Json.h>

inline namespace jf {

// What a tray feeder (OpenPnP's ReferenceTrayFeeder) has of its own: how many parts it holds across (X) and
// down (Y), the step from one to the next, and how many it has fed. Its feeder's location is the first part's.
class JPTrayFeeder {
public:
    static constexpr const char* kClass = "org.openpnp.machine.reference.feeder.ReferenceTrayFeeder";
    static constexpr const char* kKey = "tray";

    int        countX = 1, countY = 1;
    JPLocation offsets { JPLengthUnit::Millimeters };
    // OpenPnP's: 0 before the first feed (a pick always follows a feed, which makes it 1).
    int        feedCount = 0;

    // How many parts it holds (each count at least 1).
    int capacity() const;
    bool empty() const { return feedCount >= capacity(); }
    // Where the part fed last is picked, the first part at `first`: as OpenPnP's, along the longer side first
    // (down the columns when there are as many or more across); none fed yet, the first; past the end, the last.
    JPLocation pickLocation(const JPLocation& first) const;

    // OpenPnP's settings of it (its <feeder>'s own, what every feeder has taken out).
    static JPTrayFeeder fromXml(const JPXmlNode& own);
    // As jplacer's feeders.json keeps it: {"countX", "countY", "offsets", "feedCount"}.
    static JPTrayFeeder fromJson(const JJson& j);
    JJson toJson() const;
};

} // inline namespace jf
