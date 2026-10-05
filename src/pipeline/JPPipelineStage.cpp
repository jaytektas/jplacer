// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPPipelineStage.h"

#include "JPStageRegistry.h"

#include <cstdlib>
#include <sstream>

inline namespace jf {

JPPipelineStage JPPipelineStage::create(const std::string& className, const std::string& name) {
    JPXmlNode n("cv-stage");
    const JPStageType* type = JPStageRegistry::instance().find(className);
    n.attr("class", type ? type->className : className).attr("name", name).attr("enabled", "true");
    JPPipelineStage s(std::move(n));
    // Every setting written, as OpenPnP writes a new stage.
    if (type)
        for (const JPStageType::Property& p : type->properties) {
            if (p.kind == JPStageType::Kind::Color) {
                if (p.def.empty()) continue;   // not set: each item its own colour
                int c[4] = { 0, 0, 0, 255 };
                std::istringstream in(p.def);
                std::string part;
                for (int i = 0; i < 4 && std::getline(in, part, ','); ++i) c[i] = std::atoi(part.c_str());
                s.setColor(p.attribute, c[0], c[1], c[2], c[3]);
            } else {
                s.set(p.attribute, p.def);
            }
        }
    return s;
}

std::string JPPipelineStage::className() const {
    const std::string* c = m_node.get("class");
    return c ? *c : std::string();
}

std::string JPPipelineStage::typeName() const {
    const std::string c = className();
    const size_t dot = c.rfind('.');
    return dot == std::string::npos ? c : c.substr(dot + 1);
}

std::string JPPipelineStage::name() const {
    const std::string* n = m_node.get("name");
    return n ? *n : std::string();
}

bool JPPipelineStage::enabled() const {
    const std::string* e = m_node.get("enabled");
    return !e || *e == "true";
}

std::string JPPipelineStage::defaultOf(const std::string& attribute) const {
    const JPStageType* type = JPStageRegistry::instance().find(className());
    const JPStageType::Property* p = type ? type->property(attribute) : nullptr;
    return p ? p->def : std::string();
}

std::string JPPipelineStage::text(const std::string& attribute) const {
    const std::string* v = m_node.get(attribute);
    return v ? *v : defaultOf(attribute);
}

double JPPipelineStage::number(const std::string& attribute) const { return std::strtod(text(attribute).c_str(), nullptr); }

int JPPipelineStage::integer(const std::string& attribute) const { return int(std::lround(number(attribute))); }

bool JPPipelineStage::flag(const std::string& attribute) const { return text(attribute) == "true"; }

cv::Scalar JPPipelineStage::color(const std::string& element) const {
    int c[4] = { 0, 0, 0, 255 };
    if (const JPXmlNode* n = m_node.child(element)) {
        const char* keys[4] = { "r", "g", "b", "a" };
        for (int i = 0; i < 4; ++i)
            if (const std::string* v = n->get(keys[i])) c[i] = std::atoi(v->c_str());
    } else {
        std::istringstream in(defaultOf(element));
        std::string part;
        for (int i = 0; i < 4 && std::getline(in, part, ','); ++i) c[i] = std::atoi(part.c_str());
    }
    // OpenCV's order: blue, green, red.
    return cv::Scalar(c[2], c[1], c[0], c[3]);
}

void JPPipelineStage::setColor(const std::string& element, int r, int g, int b, int a) {
    JPXmlNode c(element);
    c.attr("r", std::to_string(r)).attr("g", std::to_string(g)).attr("b", std::to_string(b)).attr("a", std::to_string(a));
    if (JPXmlNode* old = m_node.child(element)) *old = c;
    else m_node.add(std::move(c));
}

} // inline namespace jf
