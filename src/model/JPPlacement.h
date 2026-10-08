// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPLocation.h"
#include "JPSide.h"

#include "openpnp/JPXmlElement.h"
#include "openpnp/JPXmlNode.h"

#include <j/config/Json.h>

#include <optional>
#include <string>

inline namespace jf {

// A placement on a board or panel, as OpenPnP's Placement: its id
// (designator), side, location, part (by id), type, comments, error
// handling, whether it is enabled, and its rank.
class JPPlacement {
public:
    enum class Type { Placement, Fiducial };
    enum class ErrorHandling { Default, Alert, Defer };

    std::string                id;
    JPSide                     side = JPSide::Top;
    JPLocation                 location;
    std::string                partId;      // the part it is placed with (its board part's: JPBoard::syncParts)
    std::string                boardPart;   // its board's part (JPBoardPart::key); empty: none yet
    Type                       type = Type::Placement;
    std::optional<std::string> comments;
    ErrorHandling              errorHandling = ErrorHandling::Default;
    bool                       enabled = true;
    int                        rank = 0;

    static const char* typeName(Type t) { return t == Type::Fiducial ? "Fiducial" : "Placement"; }
    static const char* errorHandlingName(ErrorHandling e);
    static ErrorHandling errorHandlingFrom(const std::string& s);

    // Older files' types: "Place" is a placement, "Ignore" a placement not
    // enabled.
    static JPPlacement fromXml(const JPXmlElement& e);
    JPXmlNode toXml() const;
    // In a board file of jplacer's: its board part by key, not a part id.
    static JPPlacement fromJson(const JJson& j);
    JJson toJson() const;
};

} // inline namespace jf
