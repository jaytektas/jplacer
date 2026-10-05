// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPAxisConfig.h"

#include <algorithm>
#include <cmath>

inline namespace jf {

namespace {

constexpr JPAxisConfig::Kind kKinds[] = { JPAxisConfig::Kind::Controller, JPAxisConfig::Kind::Virtual,
                                          JPAxisConfig::Kind::Mapped, JPAxisConfig::Kind::Cam };
constexpr JPAxisConfig::Type kTypes[] = { JPAxisConfig::Type::X, JPAxisConfig::Type::Y,
                                          JPAxisConfig::Type::Z, JPAxisConfig::Type::Rotation };

} // namespace

const char* JPAxisConfig::kindName(Kind k) {
    switch (k) {
        case Kind::Controller: return "controller";
        case Kind::Virtual:    return "virtual";
        case Kind::Mapped:     return "mapped";
        case Kind::Cam:        return "cam";
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

namespace {
// OpenPnP's cam: the angle it is kept short of 90 by, and how gently it goes on beyond.
constexpr double kAngleCutoff = 89.99;
constexpr double kExtensionSlope = 0.0001 / 180.0;
constexpr double kDeg = 3.14159265358979323846 / 180.0;
}

std::optional<double> JPAxisConfig::mapped(double input) const {
    if (kind == Kind::Cam) {
        const double sinusCutoff = std::sin(kAngleCutoff * kDeg);
        double t = camClockwise ? -input : input;
        t += 90.0 - camArmsAngle / 2.0;
        if (t <= -kAngleCutoff) t = (t + kAngleCutoff) * kExtensionSlope - sinusCutoff;
        else if (t >= kAngleCutoff) t = (t - kAngleCutoff) * kExtensionSlope + sinusCutoff;
        else t = std::sin(t * kDeg);
        return t * camRadius + camWheelRadius + camWheelGap;
    }
    const double span = mapInput1 - mapInput0;
    if (span == 0) return std::nullopt;
    return mapOutput0 + (input - mapInput0) * (mapOutput1 - mapOutput0) / span;
}

std::optional<double> JPAxisConfig::unmapped(double output) const {
    if (kind == Kind::Cam) {
        if (camRadius == 0) return std::nullopt;
        const double sinusCutoff = std::sin(kAngleCutoff * kDeg);
        double r = (output - camWheelRadius - camWheelGap) / camRadius;
        if (r <= -sinusCutoff) r = (r + sinusCutoff) / kExtensionSlope - kAngleCutoff;
        else if (r >= sinusCutoff) r = (r - sinusCutoff) / kExtensionSlope + kAngleCutoff;
        else r = std::asin(r) / kDeg;
        r -= 90.0 - camArmsAngle / 2.0;
        if (camClockwise) r = -r;
        // Its useful range.
        const double range = 180.0 - camArmsAngle / 2.0;
        return std::max(-range, std::min(range, r));
    }
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
    a.backlash               = backlashFromWord(j["backlash"].str());
    a.sneakUpMm              = j["sneakUp"].number(0.0);
    for (const JJson& p : j["backlashTable"].arr()) a.backlashTable.push_back({ p[0].number(), p[1].number() });
    a.approachMm             = j["approachMm"].number(0.0);
    if (j["backlashCalibration"].isObject()) a.backlashCalibration = JPBacklashCalibration::fromJson(j["backlashCalibration"]);
    a.backlashSpeedFactor    = j["backlashSpeedFactor"].number(1.0);   // 1.0, not 1: JJson::number<int> would truncate
    a.feedratePerSecond      = j["feedratePerSecond"].number();
    a.accelerationPerSecond2 = j["accelerationPerSecond2"].number();
    a.jerkPerSecond3         = j["jerkPerSecond3"].number();
    a.wrapAroundRotation     = j["wrapAroundRotation"].boolean();
    a.limitRotation          = j["limitRotation"].boolean();
    a.resolution             = j["resolution"].number();

    a.inputAxisId = j["inputAxis"].str();
    const JJson& map = j["map"];
    a.mapInput0  = map["input0"].number(a.mapInput0);
    a.mapOutput0 = map["output0"].number(a.mapOutput0);
    a.mapInput1  = map["input1"].number(a.mapInput1);
    a.mapOutput1 = map["output1"].number(a.mapOutput1);
    const JJson& cam = j["cam"];
    a.camRadius      = cam["radius"].number(a.camRadius);
    a.camArmsAngle   = cam["armsAngle"].number(a.camArmsAngle);
    a.camWheelRadius = cam["wheelRadius"].number(a.camWheelRadius);
    a.camWheelGap    = cam["wheelGap"].number(a.camWheelGap);
    a.camClockwise   = cam["clockwise"].boolean();

    if (a.kind == Kind::Controller && (a.driverId.empty() || a.letter.empty())) {
        error = "axis " + a.name + ": a controller axis needs a controller and a letter";
        return std::nullopt;
    }
    if (a.transformed() && a.inputAxisId.empty()) {
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
        j["backlash"]               = backlashWord(backlash);
        if (sneakUpMm != 0) j["sneakUp"] = sneakUpMm;
        if (!backlashTable.empty()) {
            j["backlashTable"] = JJson::array();
            for (const auto& [t, l] : backlashTable) {
                JJson p = JJson::array();
                p.push(JJson(t));
                p.push(JJson(l));
                j["backlashTable"].push(p);
            }
            j["approachMm"] = approachMm;
        }
        if (backlashCalibration) j["backlashCalibration"] = backlashCalibration->toJson();
        j["backlashSpeedFactor"]    = backlashSpeedFactor;
        j["feedratePerSecond"]      = feedratePerSecond;
        j["accelerationPerSecond2"] = accelerationPerSecond2;
        j["jerkPerSecond3"]         = jerkPerSecond3;
        j["wrapAroundRotation"]     = wrapAroundRotation;
        j["limitRotation"]          = limitRotation;
        if (resolution > 0) j["resolution"] = resolution;
    }
    if (kind == Kind::Cam) {
        j["inputAxis"] = inputAxisId;
        j["cam"]["radius"] = camRadius;
        j["cam"]["armsAngle"] = camArmsAngle;
        if (camWheelRadius != 0) j["cam"]["wheelRadius"] = camWheelRadius;
        if (camWheelGap != 0) j["cam"]["wheelGap"] = camWheelGap;
        if (camClockwise) j["cam"]["clockwise"] = true;
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

const char* JPAxisConfig::backlashWord(Backlash b) {
    switch (b) {
        case Backlash::None:               return "none";
        case Backlash::OneSided:           return "oneSided";
        case Backlash::OneSidedOptimized:  return "oneSidedOptimized";
        case Backlash::Directional:        return "directional";
        case Backlash::DirectionalSneakUp: return "directionalSneakUp";
        case Backlash::DistanceAware:      return "distanceAware";
    }
    return "none";
}

JPAxisConfig::Backlash JPAxisConfig::backlashFromWord(const std::string& w) {
    for (Backlash b : { Backlash::OneSided, Backlash::OneSidedOptimized, Backlash::Directional, Backlash::DirectionalSneakUp,
                        Backlash::DistanceAware })
        if (w == backlashWord(b)) return b;
    return Backlash::None;
}

double JPAxisConfig::lagAfter(double travel) const {
    const auto& t = backlashTable;
    if (t.empty()) return 0;
    if (travel <= t.front().first) return t.front().second;
    if (travel >= t.back().first) return t.back().second;
    for (size_t i = 1; i < t.size(); ++i)
        if (travel <= t[i].first) {
            const double a = std::log(t[i - 1].first), b = std::log(t[i].first);
            const double f = b > a ? (std::log(travel) - a) / (b - a) : 1;
            return t[i - 1].second + f * (t[i].second - t[i - 1].second);
        }
    return t.back().second;
}

double JPAxisConfig::travelFor(double lag) const {
    const auto& t = backlashTable;
    if (t.empty() || lag <= t.front().second) return 0;
    if (lag >= t.back().second) return t.back().first;
    for (size_t i = 1; i < t.size(); ++i)
        if (lag <= t[i].second) {
            const double l0 = t[i - 1].second, l1 = t[i].second;
            const double a = std::log(t[i - 1].first), b = std::log(t[i].first);
            const double f = l1 > l0 ? (lag - l0) / (l1 - l0) : 0;
            return std::exp(a + f * (b - a));
        }
    return t.back().first;
}

double JPAxisConfig::lagMoved(double lag, double from, double to) const {
    const double d = to - from;
    if (d == 0) return lag;
    const double dir = d > 0 ? 1 : -1;
    // Along the way it goes, the lag is so far along the curve; it goes on by the travel.
    return dir * lagAfter(travelFor(dir * lag) + std::abs(d));
}

} // inline namespace jf
