// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPXmlJson.h"

#include "JPXmlWriter.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <set>

inline namespace jf {

namespace {

constexpr const char* kText = "#text";
constexpr const char* kChildren = "#children";
constexpr const char* kName = "#name";

bool isInteger(const std::string& t) {
    size_t i = t.starts_with('-') ? 1 : 0;
    const size_t digits = t.size() - i;
    if (digits == 0 || digits > 15) return false;
    if (t[i] == '0' && digits > 1) return false;
    for (; i < t.size(); ++i)
        if (t[i] < '0' || t[i] > '9') return false;
    return t != "-0";
}

JJson scalar(const std::string& t) {
    if (t == "true") return JJson(true);
    if (t == "false") return JJson(false);
    if (isInteger(t)) return JJson(std::strtod(t.c_str(), nullptr));
    if (!t.empty()) {
        char* end = nullptr;
        const double v = std::strtod(t.c_str(), &end);
        if (end == t.c_str() + t.size() && std::isfinite(v) && JPXmlWriter::number(v) == t) return JJson(v);
    }
    return JJson(t);
}

std::string text(const JJson& j) {
    if (j.isBool()) return j.boolean() ? "true" : "false";
    if (j.isNumber()) {
        const double v = j.number();
        if (v == std::floor(v) && std::fabs(v) < 1e15) return std::to_string(int64_t(v));
        return JPXmlWriter::number(v);
    }
    return j.str();
}

bool bare(const JPXmlNode& c) { return c.attributes.empty() && c.children.empty(); }

} // namespace

JJson JPXmlJson::from(const JPXmlNode& n) {
    JJson j = JJson::object();
    std::set<std::string> taken;
    for (const auto& [k, v] : n.attributes) {
        j[k] = scalar(v);
        taken.insert(k);
    }
    // Text between children that is only the file's layout is not kept.
    const bool layout = !n.children.empty() && n.text.find_first_not_of(" \t\r\n") == std::string::npos;
    if (!n.text.empty() && !layout) j[kText] = scalar(n.text);
    if (n.children.empty()) return j;
    // Each name's children, in the order the names first come.
    std::vector<std::pair<std::string, std::vector<const JPXmlNode*>>> groups;
    bool clash = false;
    for (const JPXmlNode& c : n.children) {
        auto it = std::find_if(groups.begin(), groups.end(), [&c](const auto& g) { return g.first == c.name; });
        if (it == groups.end()) {
            clash = clash || taken.count(c.name) || c.name.starts_with('#');
            groups.push_back({ c.name, { &c } });
        } else {
            it->second.push_back(&c);
        }
    }
    if (clash) {
        JJson all = JJson::array();
        for (const JPXmlNode& c : n.children) {
            JJson one = JJson::object();
            one[kName] = c.name;
            const JJson of = from(c);
            for (const auto& [k, v] : of.obj()) one[k] = v;
            all.push(one);
        }
        j[kChildren] = all;
        return j;
    }
    for (const auto& [name, cs] : groups) {
        if (cs.size() == 1) {
            j[name] = from(*cs.front());
            continue;
        }
        JJson many = JJson::array();
        for (const JPXmlNode* c : cs) many.push(bare(*c) ? scalar(c->text) : from(*c));
        j[name] = many;
    }
    return j;
}

JPXmlElement JPXmlJson::element(const std::string& name, const JJson& j) {
    JPXmlElement e;
    e.name = name;
    if (!j.isObject()) {
        if (!j.isNull()) e.text = text(j);
        return e;
    }
    for (const auto& [k, v] : j.obj()) {
        if (k == kName) continue;
        if (k == kText) {
            e.text = text(v);
        } else if (k == kChildren) {
            for (const JJson& c : v.arr()) e.children.push_back(element(c[kName].str(), c));
        } else if (v.isObject()) {
            e.children.push_back(element(k, v));
        } else if (v.isArray()) {
            for (const JJson& c : v.arr()) e.children.push_back(element(k, c));
        } else {
            e.attributes[k] = text(v);
            e.attributeOrder.push_back(k);
        }
    }
    return e;
}

} // inline namespace jf
