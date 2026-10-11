// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPLocation.h"

#include "openpnp/JPXmlNode.h"

#include <j/config/Json.h>

#include <string>

inline namespace jf {

// What a rotated tray feeder (OpenPnP's ReferenceRotatedTrayFeeder) has of its own: a tray lying at any angle,
// its first part (point A) at its feeder's location, that location's rotation the tray's. Its rows and columns,
// the step from part to part (X along a row, Y from row to row), how many it has fed, how its parts lie in it
// (turned from its A→B axis), the last part of its first row (B) and its last part (C).
class JPRotatedTrayFeeder {
public:
    static constexpr const char* kClass = "org.openpnp.machine.reference.feeder.ReferenceRotatedTrayFeeder";
    static constexpr const char* kKey = "rotatedTray";

    int        cols = 1, rows = 1;
    JPLocation offsets { JPLengthUnit::Millimeters };
    int        feedCount = 0;
    double     componentRotation = 0;
    // An older OpenPnP's tray part way through: picked along its columns, as it was begun.
    bool       legacyPickingInProgress = false;
    JPLocation firstRowLast { JPLengthUnit::Millimeters }, last { JPLengthUnit::Millimeters };

    int capacity() const;
    bool empty() const { return feedCount >= capacity(); }
    // Where the part fed last is picked, the tray at `a` (point A, and its rotation): along a row, then the next.
    JPLocation pickLocation(const JPLocation& a) const;
    // OpenPnP's calculateOffsetsAndRotation: the steps from points A (`a`), B and C, and the tray's turn
    // (`rotation`, `a`'s when it has one part); false, and why, when the points and counts disagree.
    bool calculateOffsets(const JPLocation& a, double& rotation, std::string& why);

    // OpenPnP's settings of it (its <feeder>'s own, what every feeder has taken out).
    static JPRotatedTrayFeeder fromXml(const JPXmlNode& own);
    // As jplacer's feeders.json keeps it: {"cols", "rows", "offsets", "feedCount", "componentRotation",
    // "legacyPickingInProgress", "firstRowLast", "last"}.
    static JPRotatedTrayFeeder fromJson(const JJson& j);
    JJson toJson() const;
};

} // inline namespace jf
