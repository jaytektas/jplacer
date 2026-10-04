// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPLengthUnit.h"
#include "JPLocation.h"
#include "JPSide.h"

#include "openpnp/JPXmlElement.h"
#include "openpnp/JPXmlNode.h"

#include <optional>
#include <string>

inline namespace jf {

// A solder paste pad on a board, as OpenPnP's BoardPad (from a Gerber
// paste layer or an EAGLE board): its type, side, location, name and
// shape, which is OpenPnP's Pad: a round rectangle, a circle or an ellipse,
// in its own units.
class JPBoardPad {
public:
    enum class Type { Paste, Ignore };
    struct Shape {
        enum class Kind { RoundRectangle, Circle, Ellipse };
        Kind         kind = Kind::RoundRectangle;
        JPLengthUnit units = JPLengthUnit::Millimeters;
        double       width = 0;       // a round rectangle's or an ellipse's
        double       height = 0;
        double       roundness = 0;   // a round rectangle's
        double       radius = 0;      // a circle's
    };

    Type                       type = Type::Paste;
    JPSide                     side = JPSide::Top;
    JPLocation                 location;
    std::optional<std::string> name;
    Shape                      pad;

    static JPBoardPad fromXml(const JPXmlElement& e);
    JPXmlNode toXml() const;
};

} // inline namespace jf
