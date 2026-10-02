// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include <map>
#include <string>
#include <vector>

inline namespace jf {

// One element of a parsed XML document: its name, attributes, text and
// children, in document order. Built by JPXmlReader.
struct JPXmlElement {
    std::string                        name;
    std::map<std::string, std::string> attributes;
    std::string                        text;
    std::vector<JPXmlElement>          children;

    // The attribute's value, or empty.
    const std::string& attr(const std::string& key) const {
        static const std::string empty;
        const auto it = attributes.find(key);
        return it == attributes.end() ? empty : it->second;
    }
    // The first child called `child`, or null.
    const JPXmlElement* child(const std::string& childName) const {
        for (const JPXmlElement& c : children) if (c.name == childName) return &c;
        return nullptr;
    }
};

} // inline namespace jf
