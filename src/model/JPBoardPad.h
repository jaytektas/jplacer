// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPFootprint.h"
#include "JPLocation.h"
#include "JPSide.h"

#include "openpnp/JPXmlElement.h"
#include "openpnp/JPXmlNode.h"

#include <optional>
#include <string>

inline namespace jf {

// A solder paste pad on a board, as OpenPnP's BoardPad (from a Gerber
// paste layer): its type, side, location, name and shape.
class JPBoardPad {
public:
    enum class Type { Paste, Ignore };

    Type                       type = Type::Paste;
    JPSide                     side = JPSide::Top;
    JPLocation                 location;
    std::optional<std::string> name;
    JPFootprint::Pad           pad;

    static JPBoardPad fromXml(const JPXmlElement& e);
    JPXmlNode toXml() const;
};

} // inline namespace jf
