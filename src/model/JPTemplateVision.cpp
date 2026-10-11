// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPTemplateVision.h"

#include "JPLocationJson.h"
#include "JPLocationXml.h"

#include <cstdlib>

inline namespace jf {

JPTemplateVision JPTemplateVision::fromXml(const JPXmlNode* vision) {
    JPTemplateVision v;
    if (!vision) return v;
    if (const std::string* e = vision->get("enabled")) v.enabled = *e == "true";
    if (const std::string* n = vision->get("template-image-name")) v.templateImageName = *n;
    if (const JPXmlNode* a = vision->child("area-of-interest")) {
        auto integer = [a](const char* k) {
            const std::string* s = a->get(k);
            return s && !s->empty() ? std::atoi(s->c_str()) : 0;
        };
        v.areaOfInterest = { integer("x"), integer("y"), integer("width"), integer("height") };
    }
    if (const JPXmlNode* l = vision->child("template-image-top-left")) v.templateImageTopLeft = JPLocationXml::from(*l);
    if (const JPXmlNode* l = vision->child("template-image-bottom-right")) v.templateImageBottomRight = JPLocationXml::from(*l);
    return v;
}

JPTemplateVision JPTemplateVision::fromJson(const JJson& j) {
    JPTemplateVision v;
    v.enabled = j["enabled"].boolean(false);
    v.templateImageName = j["templateImage"].str();
    const JJson& a = j["areaOfInterest"];
    v.areaOfInterest = { int(a["x"].number(0)), int(a["y"].number(0)), int(a["width"].number(0)), int(a["height"].number(0)) };
    v.templateImageTopLeft = JPLocationJson::from(j["templateTopLeft"]);
    v.templateImageBottomRight = JPLocationJson::from(j["templateBottomRight"]);
    return v;
}

JJson JPTemplateVision::toJson() const {
    JJson j = JJson::object();
    j["enabled"] = enabled;
    j["templateImage"] = templateImageName;
    JJson a = JJson::object();
    a["x"] = areaOfInterest.x;
    a["y"] = areaOfInterest.y;
    a["width"] = areaOfInterest.width;
    a["height"] = areaOfInterest.height;
    j["areaOfInterest"] = a;
    j["templateTopLeft"] = JPLocationJson::to(templateImageTopLeft);
    j["templateBottomRight"] = JPLocationJson::to(templateImageBottomRight);
    return j;
}

} // inline namespace jf
