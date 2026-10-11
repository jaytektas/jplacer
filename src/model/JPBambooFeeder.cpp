// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPBambooFeeder.h"

#include <cstdlib>

inline namespace jf {

JPBambooFeeder JPBambooFeeder::fromXml(const JPXmlNode& own) {
    JPBambooFeeder b;
    b.readXml(own);
    if (const std::string* v = own.get("feed-actuator-name")) b.feedActuatorName = *v;
    if (const std::string* v = own.get("feed-actuator-value"); v && !v->empty()) b.feedActuatorValue = std::strtod(v->c_str(), nullptr);
    if (const std::string* v = own.get("post-pick-actuator-name")) b.postPickActuatorName = *v;
    if (const std::string* v = own.get("post-pick-actuator-value"); v && !v->empty())
        b.postPickActuatorValue = std::strtod(v->c_str(), nullptr);
    if (const std::string* v = own.get("move-before-feed")) b.moveBeforeFeed = *v == "true";
    return b;
}

JPBambooFeeder JPBambooFeeder::fromJson(const JJson& j) {
    JPBambooFeeder b;
    b.readJson(j);
    b.feedActuatorName = j["feedActuator"].str();
    b.feedActuatorValue = j["feedActuatorValue"].number(0.0);
    b.postPickActuatorName = j["postPickActuator"].str();
    b.postPickActuatorValue = j["postPickActuatorValue"].number(0.0);
    b.moveBeforeFeed = j["moveBeforeFeed"].boolean(false);
    return b;
}

JJson JPBambooFeeder::toJson() const {
    JJson j = json();
    j["feedActuator"] = feedActuatorName;
    j["feedActuatorValue"] = feedActuatorValue;
    j["postPickActuator"] = postPickActuatorName;
    j["postPickActuatorValue"] = postPickActuatorValue;
    j["moveBeforeFeed"] = moveBeforeFeed;
    return j;
}

} // inline namespace jf
