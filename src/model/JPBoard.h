// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPBoardPad.h"
#include "JPPlacementsHolder.h"

#include "openpnp/JPXmlElement.h"
#include "openpnp/JPXmlNode.h"

#include <memory>
#include <vector>

inline namespace jf {

// A board, as OpenPnP's Board (an .board.xml file): its placements and
// fiducials, dimensions, outline and solder paste pads.
class JPBoard : public JPPlacementsHolder {
public:
    JPBoard() = default;

    Kind kind() const override { return Kind::Board; }
    std::shared_ptr<JPPlacementsHolder> instance() const override;

    std::vector<JPBoardPad> solderPastePads;

    static JPBoard fromXml(const JPXmlElement& root);
    JPXmlNode toXml() const;
};

} // inline namespace jf
