// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPLength.h"

#include "openpnp/JPXmlElement.h"
#include "openpnp/JPXmlNode.h"

#include <string>

inline namespace jf {

// How bottom vision puts several pictures of a big part together, as
// OpenPnP's VisionCompositing on a package: the method, how far a pick may
// be off, the least leverage, extra shots, and whether corners may be seen
// from inside.
class JPVisionCompositing {
public:
    enum class Method { None, Restricted, Body, Automatic, SingleCorners };

    Method   compositingMethod = Method::Restricted;
    JPLength maxPickTolerance { 0, JPLengthUnit::Millimeters };
    double   minLeverageFactor = 0.2;
    int      extraShots = 0;
    bool     allowInside = true;

    static const char* methodName(Method m);
    static Method      methodFrom(const std::string& s);
    // Whether the method makes compositing happen however the part lies.
    static bool isEnforced(Method m) { return m == Method::Body || m == Method::Automatic || m == Method::SingleCorners; }

    static JPVisionCompositing fromXml(const JPXmlElement& e);
    JPXmlNode toXml() const;
};

} // inline namespace jf
