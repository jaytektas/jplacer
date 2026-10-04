// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "openpnp/JPXmlElement.h"

#include <string>

inline namespace jf {

// One of OpenPnP's vision settings (vision-settings.xml), as far as parts
// and packages name them: its id, name, and whether it is for bottom vision
// or fiducials. The pipelines inside are the Vision tab's.
class JPVisionSettings {
public:
    enum class Kind { Bottom, Fiducial, Other };

    std::string id;
    std::string name;
    Kind        kind = Kind::Other;
    bool        enabled = true;

    static JPVisionSettings fromXml(const JPXmlElement& e);
};

} // inline namespace jf
