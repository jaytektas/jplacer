// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPLengthUnit.h"

#include "openpnp/JPXmlElement.h"

#include <string>

inline namespace jf {

// Reading and writing attribute values in OpenPnP's files: numbers as Java
// prints them, booleans as "true"/"false", units by name.
struct JPXmlValues {
    static double number(const JPXmlElement& e, const std::string& key, double fallback = 0);
    static int    integer(const JPXmlElement& e, const std::string& key, int fallback = 0);
    static bool   boolean(const JPXmlElement& e, const std::string& key, bool fallback = false);
    static JPLengthUnit units(const JPXmlElement& e, const std::string& key,
                              JPLengthUnit fallback = JPLengthUnit::Millimeters);
    static bool   has(const JPXmlElement& e, const std::string& key) { return e.attributes.count(key) != 0; }
    // Text inside an element, without the whitespace around it.
    static std::string text(const JPXmlElement& e);

    static std::string number(double v);
    static std::string boolean(bool v) { return v ? "true" : "false"; }
    static std::string units(JPLengthUnit u);
};

} // inline namespace jf
