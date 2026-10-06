// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPVisionSettings.h"

#include "JPLengthUnits.h"
#include "JPLocationXml.h"
#include "JPXmlValues.h"

#include "openpnp/JPXmlWriter.h"

#include <cstdlib>

inline namespace jf {

namespace {
constexpr const char* kBottomClass   = "org.openpnp.model.BottomVisionSettings";
constexpr const char* kFiducialClass = "org.openpnp.model.FiducialVisionSettings";
}

JPVisionSettings JPVisionSettings::fromXml(const JPXmlElement& e) {
    JPVisionSettings v;
    v.m_node = JPXmlNode::from(e);
    v.id = e.attr("id");
    v.name = e.attr("name");
    v.enabled = JPXmlValues::boolean(e, "enabled", true);
    const std::string cls = e.attr("class");
    if (cls == kBottomClass) v.kind = Kind::Bottom;
    else if (cls == kFiducialClass) v.kind = Kind::Fiducial;
    return v;
}

JPXmlNode JPVisionSettings::toXml() const {
    JPXmlNode n = m_node;
    n.name = "vision-settings";
    n.set("id", id);
    n.set("name", name);
    n.set("enabled", enabled ? "true" : "false");
    return n;
}

JPVisionSettings JPVisionSettings::create(Kind kind, const std::string& newId) {
    JPVisionSettings v;
    v.kind = kind;
    v.id = newId;
    v.m_node = JPXmlNode("vision-settings");
    v.m_node.attr("class", kind == Kind::Bottom ? kBottomClass : kFiducialClass).attr("id", newId);
    v.name = kind == Kind::Bottom ? "BottomVisionSettings" : "FiducialVisionSettings";
    v.m_node.attr("name", v.name).attr("enabled", "true");
    if (kind == Kind::Bottom)
        v.m_node.attr("pre-rotate-usage", "Default").attr("check-part-size-method", "Disabled")
            .attr("check-size-tolerance-percent", "20").attr("max-rotation", "Adjust").attr("asymmetric", "false");
    else
        v.m_node.attr("parallax-angle", "0.0").attr("max-vision-passes", "3");
    return v;
}

std::string JPVisionSettings::className() const { return text("class"); }

void JPVisionSettings::setPipeline(JPXmlNode pipeline) {
    pipeline.name = "cv-pipeline";
    if (JPXmlNode* c = m_node.child("cv-pipeline")) {
        *c = std::move(pipeline);
        return;
    }
    m_node.children.insert(m_node.children.begin(), std::move(pipeline));
}

void JPVisionSettings::setParameterAssignments(JPXmlNode assignments) {
    assignments.name = "pipeline-parameter-assignments";
    if (JPXmlNode* c = m_node.child("pipeline-parameter-assignments")) {
        *c = std::move(assignments);
        return;
    }
    // After the pipeline, as OpenPnP writes them.
    auto at = m_node.children.begin();
    if (!m_node.children.empty() && m_node.children.front().name == "cv-pipeline") ++at;
    m_node.children.insert(at, std::move(assignments));
}

std::string JPVisionSettings::text(const std::string& attribute, const std::string& def) const {
    const std::string* v = m_node.get(attribute);
    return v ? *v : def;
}

void JPVisionSettings::setText(const std::string& attribute, const std::string& value) { m_node.set(attribute, value); }

int JPVisionSettings::number(const std::string& attribute, int def) const {
    const std::string* v = m_node.get(attribute);
    return v && !v->empty() ? std::atoi(v->c_str()) : def;
}

double JPVisionSettings::real(const std::string& attribute, double def) const {
    const std::string* v = m_node.get(attribute);
    return v && !v->empty() ? std::strtod(v->c_str(), nullptr) : def;
}

bool JPVisionSettings::flag(const std::string& attribute, bool def) const {
    const std::string* v = m_node.get(attribute);
    return v ? *v == "true" : def;
}

double JPVisionSettings::lengthMm(const std::string& element, double def) const {
    const JPXmlNode* c = m_node.child(element);
    if (!c || !c->get("value")) return def;
    JPLengthUnit u = JPLengthUnit::Millimeters;
    if (const std::string* units = c->get("units")) JPLengthUnits::fromName(*units, u);
    return JPLength(std::strtod(c->get("value")->c_str(), nullptr), u).convertToUnits(JPLengthUnit::Millimeters).value();
}

void JPVisionSettings::setLengthMm(const std::string& element, double mm) {
    JPXmlNode fresh(element);
    fresh.attr("value", JPXmlWriter::number(mm)).attr("units", "Millimeters");
    if (JPXmlNode* c = m_node.child(element)) *c = fresh;
    else m_node.add(fresh);
}

JPLocation JPVisionSettings::locationOf(const std::string& element) const {
    const JPXmlNode* c = m_node.child(element);
    if (!c) return JPLocation(JPLengthUnit::Millimeters);
    JPXmlElement e;
    e.name = c->name;
    for (const auto& [k, v] : c->attributes) e.attributes[k] = v;
    return JPLocationXml::from(e);
}

void JPVisionSettings::setLocationOf(const std::string& element, const JPLocation& l) {
    JPXmlNode fresh = JPLocationXml::to(element, l);
    if (JPXmlNode* c = m_node.child(element)) *c = fresh;
    else m_node.add(fresh);
}

void JPVisionSettings::setValues(const JPVisionSettings& another) {
    if (&another == this) return;
    const std::string keepId = id, keepName = name;
    *this = another;
    id = keepId;
    name = keepName;
}

} // inline namespace jf
