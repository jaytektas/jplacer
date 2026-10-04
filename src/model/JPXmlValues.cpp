// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPXmlValues.h"

#include "JPLengthUnits.h"

#include "openpnp/JPXmlWriter.h"

#include <cctype>
#include <cstdlib>

inline namespace jf {

double JPXmlValues::number(const JPXmlElement& e, const std::string& key, double fallback) {
    const auto it = e.attributes.find(key);
    if (it == e.attributes.end() || it->second.empty()) return fallback;
    char* end = nullptr;
    const double v = std::strtod(it->second.c_str(), &end);
    return end == it->second.c_str() ? fallback : v;
}

int JPXmlValues::integer(const JPXmlElement& e, const std::string& key, int fallback) {
    const auto it = e.attributes.find(key);
    if (it == e.attributes.end() || it->second.empty()) return fallback;
    char* end = nullptr;
    const long v = std::strtol(it->second.c_str(), &end, 10);
    return end == it->second.c_str() ? fallback : int(v);
}

bool JPXmlValues::boolean(const JPXmlElement& e, const std::string& key, bool fallback) {
    const auto it = e.attributes.find(key);
    if (it == e.attributes.end()) return fallback;
    std::string v;
    for (const char c : it->second) v += char(std::tolower(static_cast<unsigned char>(c)));
    return v == "true";
}

JPLengthUnit JPXmlValues::units(const JPXmlElement& e, const std::string& key, JPLengthUnit fallback) {
    JPLengthUnit u;
    return JPLengthUnits::fromName(e.attr(key), u) ? u : fallback;
}

std::string JPXmlValues::text(const JPXmlElement& e) {
    size_t a = 0, b = e.text.size();
    while (a < b && std::isspace(static_cast<unsigned char>(e.text[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(e.text[b - 1]))) --b;
    return e.text.substr(a, b - a);
}

std::string JPXmlValues::number(double v) {
    return JPXmlWriter::number(v);
}

std::string JPXmlValues::units(JPLengthUnit u) {
    return JPLengthUnits::name(u);
}

} // inline namespace jf
