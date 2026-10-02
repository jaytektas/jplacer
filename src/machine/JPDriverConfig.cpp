// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPDriverConfig.h"

inline namespace jf {

std::optional<JPDriverConfig> JPDriverConfig::fromJson(const JJson& j, std::string& error) {
    JPDriverConfig c;
    c.id   = j["id"].str();
    c.name = j["name"].str();
    if (c.id.empty()) {
        error = "a controller needs an id";
        return std::nullopt;
    }
    if (!j["link"].isObject() || j["link"]["type"].str().empty()) {
        error = "controller " + c.name + " has no link";
        return std::nullopt;
    }
    c.link = j["link"];
    if (j.contains("profile")) c.profile = j["profile"].str();
    c.statusIntervalMs  = int(j["statusIntervalMs"].number(c.statusIntervalMs));
    c.commandTimeoutMs  = int(j["commandTimeoutMs"].number(c.commandTimeoutMs));
    c.identifyTimeoutMs = int(j["identifyTimeoutMs"].number(c.identifyTimeoutMs));
    c.homeTimeoutMs     = int(j["homeTimeoutMs"].number(c.homeTimeoutMs));
    return c;
}

JJson JPDriverConfig::toJson() const {
    JJson j = JJson::object();
    j["id"]                = id;
    j["name"]              = name;
    j["profile"]           = profile;
    j["link"]              = link;
    j["statusIntervalMs"]  = statusIntervalMs;
    j["commandTimeoutMs"]  = commandTimeoutMs;
    j["identifyTimeoutMs"] = identifyTimeoutMs;
    j["homeTimeoutMs"]     = homeTimeoutMs;
    return j;
}

} // inline namespace jf
