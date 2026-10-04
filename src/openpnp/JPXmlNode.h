// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPXmlElement.h"

#include <string>
#include <utility>
#include <vector>

inline namespace jf {

// An XML element to be written (JPXmlWriter): its name, its attributes in
// the order given, text, and children. OpenPnP's files are written as its
// serializer writes them, attribute by attribute in field order.
struct JPXmlNode {
    std::string                                      name;
    std::vector<std::pair<std::string, std::string>> attributes;
    std::string                                      text;
    std::vector<JPXmlNode>                           children;

    JPXmlNode() = default;
    explicit JPXmlNode(std::string n) : name(std::move(n)) {}

    JPXmlNode& attr(const std::string& key, const std::string& value) {
        attributes.emplace_back(key, value);
        return *this;
    }
    // An attribute's value, or none.
    const std::string* get(const std::string& key) const {
        for (const auto& [k, v] : attributes)
            if (k == key) return &v;
        return nullptr;
    }
    // An attribute set where it is, or added after the others.
    JPXmlNode& set(const std::string& key, const std::string& value) {
        for (auto& [k, v] : attributes)
            if (k == key) {
                v = value;
                return *this;
            }
        return attr(key, value);
    }
    // The first child called `childName`, or none.
    JPXmlNode*       child(const std::string& childName) {
        for (JPXmlNode& c : children)
            if (c.name == childName) return &c;
        return nullptr;
    }
    const JPXmlNode* child(const std::string& childName) const {
        for (const JPXmlNode& c : children)
            if (c.name == childName) return &c;
        return nullptr;
    }
    JPXmlNode& add(JPXmlNode child) {
        children.push_back(std::move(child));
        return children.back();
    }
    // An element as read, to be written back as it was (its attributes in
    // the file's order).
    static JPXmlNode from(const JPXmlElement& e) {
        JPXmlNode n(e.name);
        if (e.attributeOrder.size() == e.attributes.size())
            for (const std::string& k : e.attributeOrder) n.attributes.emplace_back(k, e.attr(k));
        else
            for (const auto& [k, v] : e.attributes) n.attributes.emplace_back(k, v);
        n.text = e.text;
        for (const JPXmlElement& c : e.children) n.children.push_back(from(c));
        return n;
    }
};

} // inline namespace jf
