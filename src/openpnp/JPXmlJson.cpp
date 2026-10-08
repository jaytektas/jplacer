// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPXmlJson.h"

inline namespace jf {

JJson JPXmlJson::from(const JPXmlNode& n) {
    JJson j = JJson::object();
    j["tag"] = n.name;
    if (!n.attributes.empty()) {
        JJson attrs = JJson::array();
        for (const auto& [k, v] : n.attributes) {
            JJson pair = JJson::array();
            pair.push(JJson(k));
            pair.push(JJson(v));
            attrs.push(pair);
        }
        j["attrs"] = attrs;
    }
    if (!n.text.empty()) j["text"] = n.text;
    if (!n.children.empty()) {
        JJson children = JJson::array();
        for (const JPXmlNode& c : n.children) children.push(from(c));
        j["children"] = children;
    }
    return j;
}

JPXmlElement JPXmlJson::element(const JJson& j) {
    JPXmlElement e;
    if (!j.isObject()) return e;
    e.name = j["tag"].isString() ? j["tag"].str() : std::string();
    if (j["attrs"].isArray())
        for (const JJson& pair : j["attrs"].arr()) {
            if (!pair.isArray() || pair.size() != 2 || !pair[0].isString() || !pair[1].isString()) continue;
            e.attributes[pair[0].str()] = pair[1].str();
            e.attributeOrder.push_back(pair[0].str());
        }
    if (j["text"].isString()) e.text = j["text"].str();
    if (j["children"].isArray())
        for (const JJson& c : j["children"].arr()) e.children.push_back(element(c));
    return e;
}

} // inline namespace jf
