// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPLength.h"

#include "openpnp/JPXmlElement.h"
#include "openpnp/JPXmlNode.h"

#include <optional>
#include <string>

inline namespace jf {

// A part, as OpenPnP's Part: its id, name, height (and the depth it reaches
// through the board), its package (by id), speed (a share of full),
// pick retries, and the vision settings it uses (by id; empty: its
// package's).
class JPPart {
public:
    std::string                id;
    std::optional<std::string> name;
    JPLength                   height { 0, JPLengthUnit::Millimeters };
    JPLength                   throughBoardDepth { 0, JPLengthUnit::Millimeters };
    std::string                packageId;
    double                     speed = 1.0;
    int                        pickRetryCount = 0;
    std::string                bottomVisionId;
    std::string                fiducialVisionId;

    // Height plus the depth through the board: what hangs below a nozzle.
    JPLength heightForSafeZ() const { return height.add(throughBoardDepth); }
    bool     isPartHeightUnknown() const { return height.value() <= 0.0; }

    static JPPart fromXml(const JPXmlElement& e);
    JPXmlNode toXml() const;
};

} // inline namespace jf
