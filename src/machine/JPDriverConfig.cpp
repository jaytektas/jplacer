// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPDriverConfig.h"

#include "JPMotionControlType.h"

#include <utility>

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
    if (j["openpnpClass"].str() == "GcodeAsyncDriver") c.gcodeClass = "GcodeAsyncDriver";
    c.statusIntervalMs  = int(j["statusIntervalMs"].number(c.statusIntervalMs));
    c.commandTimeoutMs  = int(j["commandTimeoutMs"].number(c.commandTimeoutMs));
    c.identifyTimeoutMs = int(j["identifyTimeoutMs"].number(c.identifyTimeoutMs));
    c.dollarWaitMs = int(j["dollarWaitMs"].number(c.dollarWaitMs));
    c.homeTimeoutMs     = int(j["homeTimeoutMs"].number(c.homeTimeoutMs));
    c.connectWaitMs     = int(j["connectWaitMs"].number(c.connectWaitMs));
    c.maxFeedRate       = j["maxFeedRate"].number(c.maxFeedRate);
    c.motionControlType = j["motionControlType"].isString() ? j["motionControlType"].str() : std::string("EuclideanAxisLimits");
    if (!JPMotionControlType::fromName(c.motionControlType)) c.motionControlType = "ToolpathFeedRate";
    if (const JJson& i = j["interpolation"]; i.isObject()) {
        c.interpolationMaxSteps  = int(i["maxSteps"].number(c.interpolationMaxSteps));
        c.interpolationJerkSteps = int(i["jerkSteps"].number(c.interpolationJerkSteps));
        c.interpolationMinStep   = int(i["minStep"].number(c.interpolationMinStep));
        c.interpolationTimeStep  = i["timeStep"].number(c.interpolationTimeStep);
        c.junctionDeviation      = i["junctionDeviation"].number(c.junctionDeviation);
    }
    c.syncInitialLocation = j["syncInitialLocation"].boolean();
    c.allowUnhomedMotion  = j["allowUnhomedMotion"].boolean();
    c.logGcode          = j["logGcode"].boolean(c.logGcode);
    c.removeComments    = j["removeComments"].boolean(c.removeComments);
    c.compressGcode     = j["compressGcode"].boolean(c.compressGcode);
    if (j["compressionExcludes"].isString()) c.compressionExcludes = j["compressionExcludes"].str();
    c.backslashEscapes  = j["backslashEscapes"].boolean(c.backslashEscapes);
    if (j["units"].str() == "Inches") c.units = "Inches";
    c.usingLetterVariables = j["letterVariables"].boolean(true);
    c.supportingPreMove = j["preMove"].boolean(false);
    c.keepAlive = j["keepAlive"].boolean(false);
    for (auto [key, s] : { std::pair { "sendOnChangeFeed", &c.sendOnChangeFeed }, std::pair { "sendOnChangeAcceleration", &c.sendOnChangeAcceleration },
                           std::pair { "sendOnChangeJerk", &c.sendOnChangeJerk } }) {
        s->on = j[key]["on"].boolean();
        s->relativeDeviation = j[key]["relativeDeviation"].number(0.001);
    }
    for (const auto& [name, tmpl] : j["commands"].obj()) c.commands[name] = tmpl.str();
    return c;
}

std::string JPDriverConfig::className() const {
    const std::string type = std::as_const(link)["type"].str();
    return type == "simulated" ? "NullDriver" : type == "neoden4" ? "NeoDen4Driver" : gcodeClass;
}

JJson JPDriverConfig::toJson() const {
    JJson j = JJson::object();
    j["id"]                = id;
    j["name"]              = name;
    j["profile"]           = profile;
    if (gcodeClass != "GcodeDriver") j["openpnpClass"] = gcodeClass;
    j["link"]              = link;
    j["statusIntervalMs"]  = statusIntervalMs;
    j["commandTimeoutMs"]  = commandTimeoutMs;
    j["identifyTimeoutMs"] = identifyTimeoutMs;
    j["dollarWaitMs"] = dollarWaitMs;
    j["homeTimeoutMs"]     = homeTimeoutMs;
    j["connectWaitMs"]     = connectWaitMs;
    if (maxFeedRate > 0) j["maxFeedRate"] = maxFeedRate;
    j["motionControlType"] = motionControlType;
    j["interpolation"]["maxSteps"] = interpolationMaxSteps;
    j["interpolation"]["jerkSteps"] = interpolationJerkSteps;
    j["interpolation"]["minStep"] = interpolationMinStep;
    j["interpolation"]["timeStep"] = interpolationTimeStep;
    j["interpolation"]["junctionDeviation"] = junctionDeviation;
    if (syncInitialLocation) j["syncInitialLocation"] = true;
    if (allowUnhomedMotion) j["allowUnhomedMotion"] = true;
    if (logGcode) j["logGcode"] = true;
    if (removeComments) j["removeComments"] = true;
    if (compressGcode) j["compressGcode"] = true;
    if (compressionExcludes != JPDriverConfig().compressionExcludes) j["compressionExcludes"] = compressionExcludes;
    if (backslashEscapes) j["backslashEscapes"] = true;
    if (units != "Millimeters") j["units"] = units;
    if (!usingLetterVariables) j["letterVariables"] = false;
    if (supportingPreMove) j["preMove"] = true;
    if (keepAlive) j["keepAlive"] = true;
    for (auto [key, s] : { std::pair { "sendOnChangeFeed", &sendOnChangeFeed }, std::pair { "sendOnChangeAcceleration", &sendOnChangeAcceleration },
                           std::pair { "sendOnChangeJerk", &sendOnChangeJerk } })
        if (s->on || s->relativeDeviation != 0.001) {
            j[key]["on"] = s->on;
            j[key]["relativeDeviation"] = s->relativeDeviation;
        }
    if (!commands.empty()) {
        j["commands"] = JJson::object();
        for (const auto& [name, tmpl] : commands) j["commands"][name] = tmpl;
    }
    return j;
}

} // inline namespace jf
