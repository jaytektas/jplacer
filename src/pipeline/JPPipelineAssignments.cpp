// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPPipelineAssignments.h"

#include "openpnp/JPXmlWriter.h"

#include <cmath>
#include <cstdlib>

inline namespace jf {

namespace {

// Millimetres in OpenPnP's length units, square millimetres in its area units.
double mmPer(const std::string& unit) {
    static const std::pair<const char*, double> units[] = { { "Meters", 1000 }, { "Centimeters", 10 }, { "Millimeters", 1 },
                                                            { "Feet", 304.8 },  { "Inches", 25.4 },    { "Mils", 0.0254 },
                                                            { "Microns", 0.001 } };
    for (const auto& [n, mm] : units)
        if (unit == n) return mm;
    return 1;
}

double mm2Per(const std::string& unit) {
    static const std::pair<const char*, double> units[] = { { "SquareMeters", 1e6 }, { "SquareCentimeters", 100 },
                                                            { "SquareMillimeters", 1 }, { "SquareFeet", 304.8 * 304.8 },
                                                            { "SquareInches", 25.4 * 25.4 }, { "SquareMils", 0.0254 * 0.0254 },
                                                            { "SquareMicrons", 1e-6 } };
    for (const auto& [n, mm2] : units)
        if (unit == n) return mm2;
    return 1;
}

double real(const std::string& s) { return std::strtod(s.c_str(), nullptr); }

} // namespace

JPPipelineAssignments::Map JPPipelineAssignments::fromXml(const JPXmlNode* node) {
    Map out;
    if (!node) return out;
    for (const JPXmlNode& entry : node->children) {
        if (entry.name != "entry" || entry.children.size() < 2) continue;
        const std::string name = entry.children[0].text;
        const JPXmlNode& v = entry.children[1];
        const std::string* cls = v.get("class");
        const std::string type = cls ? *cls : v.name;
        auto attr = [&v](const char* a) {
            const std::string* s = v.get(a);
            return s ? *s : std::string();
        };
        if (type == "java.lang.Integer" || type == "int" || type == "java.lang.Long" || type == "long")
            out[name] = JPPipelineValue { long(std::strtol(v.text.c_str(), nullptr, 10)) };
        else if (type == "java.lang.Double" || type == "double" || type == "java.lang.Float" || type == "float")
            out[name] = JPPipelineValue { real(v.text) };
        else if (type == "java.lang.Boolean" || type == "boolean") out[name] = JPPipelineValue { v.text == "true" };
        else if (type == "java.lang.String" || type == "string") out[name] = JPPipelineValue { v.text };
        else if (type == "org.openpnp.model.Length")
            out[name] = JPPipelineValue { JPPipelineValue::LengthMm { real(attr("value")) * mmPer(attr("units")) } };
        else if (type == "org.openpnp.model.Area")
            out[name] = JPPipelineValue { JPPipelineValue::AreaMm2 { real(attr("value")) * mm2Per(attr("units")) } };
    }
    return out;
}

JPXmlNode JPPipelineAssignments::toXml(const Map& assignments) {
    JPXmlNode node("pipeline-parameter-assignments");
    node.attr("class", "java.util.HashMap");
    for (const auto& [name, value] : assignments) {
        JPXmlNode entry("entry");
        JPXmlNode key("string");
        key.text = name;
        entry.add(std::move(key));
        JPXmlNode v("object");
        if (const long* l = std::get_if<long>(&value.value)) {
            v.attr("class", "java.lang.Integer");
            v.text = std::to_string(*l);
        } else if (const double* d = std::get_if<double>(&value.value)) {
            v.attr("class", "java.lang.Double");
            v.text = JPXmlWriter::number(*d);
        } else if (const bool* b = std::get_if<bool>(&value.value)) {
            v.attr("class", "java.lang.Boolean");
            v.text = *b ? "true" : "false";
        } else if (const std::string* s = std::get_if<std::string>(&value.value)) {
            v.attr("class", "java.lang.String");
            v.text = *s;
        } else if (const auto* len = std::get_if<JPPipelineValue::LengthMm>(&value.value)) {
            v.attr("class", "org.openpnp.model.Length").attr("value", JPXmlWriter::number(len->mm)).attr("units", "Millimeters");
        } else if (const auto* area = std::get_if<JPPipelineValue::AreaMm2>(&value.value)) {
            v.attr("class", "org.openpnp.model.Area").attr("value", JPXmlWriter::number(area->mm2)).attr("units", "SquareMillimeters");
        } else {
            continue;
        }
        entry.add(std::move(v));
        node.add(std::move(entry));
    }
    return node;
}

} // inline namespace jf
