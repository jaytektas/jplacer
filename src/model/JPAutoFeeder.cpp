// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPAutoFeeder.h"

#include <cstdlib>

inline namespace jf {

JPAutoFeeder JPAutoFeeder::fromXml(const JPXmlNode& own) {
    JPAutoFeeder a;
    auto text = [&own](const char* k) {
        const std::string* v = own.get(k);
        return v ? *v : std::string();
    };
    auto real = [&own](const char* k) {
        const std::string* v = own.get(k);
        return v && !v->empty() ? std::strtod(v->c_str(), nullptr) : 0.0;
    };
    a.actuatorName = text("actuator-name");
    a.actuatorValue = real("actuator-value");
    a.postPickActuatorName = text("post-pick-actuator-name");
    a.postPickActuatorValue = real("post-pick-actuator-value");
    a.moveBeforeFeed = text("move-before-feed") == "true";
    a.recycleSupport = text("recycle-support") == "true";
    return a;
}

JPAutoFeeder JPAutoFeeder::fromJson(const JJson& j) {
    JPAutoFeeder a;
    a.actuatorName = j["actuator"].str();
    a.actuatorValue = j["actuatorValue"].number(0.0);
    a.postPickActuatorName = j["postPickActuator"].str();
    a.postPickActuatorValue = j["postPickActuatorValue"].number(0.0);
    a.moveBeforeFeed = j["moveBeforeFeed"].boolean(false);
    a.recycleSupport = j["recycleSupport"].boolean(false);
    return a;
}

JJson JPAutoFeeder::toJson() const {
    JJson j = JJson::object();
    j["actuator"] = actuatorName;
    j["actuatorValue"] = actuatorValue;
    j["postPickActuator"] = postPickActuatorName;
    j["postPickActuatorValue"] = postPickActuatorValue;
    j["moveBeforeFeed"] = moveBeforeFeed;
    j["recycleSupport"] = recycleSupport;
    return j;
}

} // inline namespace jf
