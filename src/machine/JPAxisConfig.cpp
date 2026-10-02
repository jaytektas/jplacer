// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPAxisConfig.h"

inline namespace jf {

namespace {

constexpr JPAxisConfig::Kind kKinds[] = { JPAxisConfig::Kind::Controller, JPAxisConfig::Kind::Virtual,
                                          JPAxisConfig::Kind::Mapped };
constexpr JPAxisConfig::Type kTypes[] = { JPAxisConfig::Type::X, JPAxisConfig::Type::Y,
                                          JPAxisConfig::Type::Z, JPAxisConfig::Type::Rotation };

} // namespace

const char* JPAxisConfig::kindName(Kind k) {
    switch (k) {
        case Kind::Controller: return "controller";
        case Kind::Virtual:    return "virtual";
        case Kind::Mapped:     return "mapped";
    }
    return "";
}

const char* JPAxisConfig::typeName(Type t) {
    switch (t) {
        case Type::X:        return "x";
        case Type::Y:        return "y";
        case Type::Z:        return "z";
        case Type::Rotation: return "rotation";
    }
    return "";
}

std::optional<double> JPAxisConfig::mapped(double input) const {
    const double span = mapInput1 - mapInput0;
    if (span == 0) return std::nullopt;
    return mapOutput0 + (input - mapInput0) * (mapOutput1 - mapOutput0) / span;
}

std::optional<double> JPAxisConfig::unmapped(double output) const {
    const double span = mapOutput1 - mapOutput0;
    if (span == 0) return std::nullopt;
    return mapInput0 + (output - mapOutput0) * (mapInput1 - mapInput0) / span;
}

std::optional<JPAxisConfig> JPAxisConfig::fromJson(const JJson& j, std::string& error) {
    JPAxisConfig a;
    a.id   = j["id"].str();
    a.name = j["name"].str();
    if (a.id.empty()) {
        error = "an axis needs an id";
        return std::nullopt;
    }
    bool kindKnown = false, typeKnown = false;
    for (Kind k : kKinds) if (j["kind"].str() == kindName(k)) { a.kind = k; kindKnown = true; }
    for (Type t : kTypes) if (j["type"].str() == typeName(t)) { a.type = t; typeKnown = true; }
    if (!kindKnown || !typeKnown) {
        error = "axis " + a.name + ": unknown kind '" + j["kind"].str() + "' or type '" + j["type"].str() + "'";
        return std::nullopt;
    }

    a.driverId       = j["driver"].str();
    a.letter         = j["letter"].str();
    a.homeCoordinate = j["homeCoordinate"].number();
    const JJson& soft = j["softLimits"];
    a.softLimitLow  = soft["low"].number();
    a.softLimitHigh = soft["high"].number();
    a.softLimitLowEnabled  = soft["lowEnabled"].boolean();
    a.softLimitHighEnabled = soft["highEnabled"].boolean();
    const JJson& safe = j["safeZone"];
    a.safeZoneLow  = safe["low"].number();
    a.safeZoneHigh = safe["high"].number();
    a.safeZoneLowEnabled  = safe["lowEnabled"].boolean();
    a.safeZoneHighEnabled = safe["highEnabled"].boolean();
    a.backlashOffset         = j["backlashOffset"].number();
    a.feedratePerSecond      = j["feedratePerSecond"].number();
    a.accelerationPerSecond2 = j["accelerationPerSecond2"].number();
    a.jerkPerSecond3         = j["jerkPerSecond3"].number();
    a.wrapAroundRotation     = j["wrapAroundRotation"].boolean();
    a.limitRotation          = j["limitRotation"].boolean();

    a.inputAxisId = j["inputAxis"].str();
    const JJson& map = j["map"];
    a.mapInput0  = map["input0"].number(a.mapInput0);
    a.mapOutput0 = map["output0"].number(a.mapOutput0);
    a.mapInput1  = map["input1"].number(a.mapInput1);
    a.mapOutput1 = map["output1"].number(a.mapOutput1);

    if (a.kind == Kind::Controller && (a.driverId.empty() || a.letter.empty())) {
        error = "axis " + a.name + ": a controller axis needs a controller and a letter";
        return std::nullopt;
    }
    if (a.kind == Kind::Mapped && a.inputAxisId.empty()) {
        error = "axis " + a.name + ": a mapped axis needs an input axis";
        return std::nullopt;
    }
    return a;
}

JJson JPAxisConfig::toJson() const {
    JJson j = JJson::object();
    j["id"]   = id;
    j["name"] = name;
    j["kind"] = kindName(kind);
    j["type"] = typeName(type);
    j["homeCoordinate"] = homeCoordinate;
    if (kind == Kind::Controller) {
        j["driver"] = driverId;
        j["letter"] = letter;
        j["softLimits"]["low"]         = softLimitLow;
        j["softLimits"]["high"]        = softLimitHigh;
        j["softLimits"]["lowEnabled"]  = softLimitLowEnabled;
        j["softLimits"]["highEnabled"] = softLimitHighEnabled;
        j["safeZone"]["low"]           = safeZoneLow;
        j["safeZone"]["high"]          = safeZoneHigh;
        j["safeZone"]["lowEnabled"]    = safeZoneLowEnabled;
        j["safeZone"]["highEnabled"]   = safeZoneHighEnabled;
        j["backlashOffset"]         = backlashOffset;
        j["feedratePerSecond"]      = feedratePerSecond;
        j["accelerationPerSecond2"] = accelerationPerSecond2;
        j["jerkPerSecond3"]         = jerkPerSecond3;
        j["wrapAroundRotation"]     = wrapAroundRotation;
        j["limitRotation"]          = limitRotation;
    }
    if (kind == Kind::Mapped) {
        j["inputAxis"]      = inputAxisId;
        j["map"]["input0"]  = mapInput0;
        j["map"]["output0"] = mapOutput0;
        j["map"]["input1"]  = mapInput1;
        j["map"]["output1"] = mapOutput1;
    }
    return j;
}

} // inline namespace jf
