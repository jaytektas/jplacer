// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPTrayFeeder.h"

#include "JPLocationJson.h"
#include "JPLocationXml.h"

#include <algorithm>
#include <cstdlib>

inline namespace jf {

int JPTrayFeeder::capacity() const { return std::max(countX, 1) * std::max(countY, 1); }

JPLocation JPTrayFeeder::pickLocation(const JPLocation& first) const {
    const int cx = std::max(countX, 1), cy = std::max(countY, 1);
    int base0 = feedCount - 1;
    if (feedCount == 0) base0 = 0;
    else if (feedCount > cx * cy) base0 = cx * cy - 1;   // past the end: the last
    int px, py;
    if (cx >= cy) {
        px = base0 / cy;
        py = base0 % cy;
    } else {
        px = base0 % cx;
        py = base0 / cx;
    }
    return first.add(offsets.multiply(px, py, 0.0, 0.0));
}

JPTrayFeeder JPTrayFeeder::fromXml(const JPXmlNode& own) {
    JPTrayFeeder t;
    auto integer = [&own](const char* a, int def) {
        const std::string* v = own.get(a);
        return v && !v->empty() ? std::atoi(v->c_str()) : def;
    };
    t.countX = integer("tray-count-x", 1);
    t.countY = integer("tray-count-y", 1);
    t.feedCount = integer("feed-count", 0);
    if (const JPXmlNode* o = own.child("offsets")) t.offsets = JPLocationXml::from(*o);
    return t;
}

JPTrayFeeder JPTrayFeeder::fromJson(const JJson& j) {
    JPTrayFeeder t;
    t.countX = int(j["countX"].number(1));
    t.countY = int(j["countY"].number(1));
    t.offsets = JPLocationJson::from(j["offsets"]);
    t.feedCount = int(j["feedCount"].number(0));
    return t;
}

JJson JPTrayFeeder::toJson() const {
    JJson j = JJson::object();
    j["countX"] = countX;
    j["countY"] = countY;
    j["offsets"] = JPLocationJson::to(offsets);
    j["feedCount"] = feedCount;
    return j;
}

} // inline namespace jf
