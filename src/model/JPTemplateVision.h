// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPLocation.h"

#include "openpnp/JPXmlNode.h"

#include <j/config/Json.h>

#include <string>

inline namespace jf {

// The vision of a feeder that finds a template image near its pick location (OpenPnP's drag, lever and Neoden 4
// feeders' Vision): whether it looks, the template image's file name (in its class's vision folder), the area of
// the camera's picture it looks in (in pixels; a Neoden 4 feeder's from the picture's middle), and where the
// template was taken from.
class JPTemplateVision {
public:
    struct Area {
        int x = 0, y = 0, width = 0, height = 0;
    };

    bool        enabled = false;
    std::string templateImageName;
    Area        areaOfInterest;
    JPLocation  templateImageTopLeft { JPLengthUnit::Millimeters }, templateImageBottomRight { JPLengthUnit::Millimeters };

    // OpenPnP's <vision> (none: as a new one).
    static JPTemplateVision fromXml(const JPXmlNode* vision);
    // As jplacer's feeders.json keeps it: {"enabled", "templateImage", "areaOfInterest" {"x", "y", "width",
    // "height"}, "templateTopLeft", "templateBottomRight"}.
    static JPTemplateVision fromJson(const JJson& j);
    JJson toJson() const;
};

} // inline namespace jf
