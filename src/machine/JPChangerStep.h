// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include <j/config/Json.h>

#include <optional>
#include <string>

inline namespace jf {

// One step of loading or unloading a nozzle tip (JPNozzleTipConfig): what a
// machine's tip changer does is taught as a list of these, as it is built.
//
//   Move      the nozzle doing the change to a place: X, Y, Z and rotation,
//             each left out to stay as it is, in the axes' own coordinates
//             (not squared: a squareness measured later does not move it),
//             at `speed` (a share of top speed).
//   SafeZ     the nozzle up to safe Z, at `speed`.
//   Actuator  an actuator switched on or off (a door, a lock, a valve).
//   Wait      `waitMs` milliseconds.
//   Ask       `message` shown to the person, who carries on or cancels.
struct JPChangerStep {
    enum class Kind { Move, SafeZ, Actuator, Wait, Ask };

    Kind                  kind = Kind::Move;
    std::optional<double> x, y, z, rotation;
    double                speed = 1;
    std::string           actuatorId;
    bool                  on = true;
    int                   waitMs = 0;
    std::string           message;
    // Its place in OpenPnP's tool changer (JPNozzleTipConfig::OpenPnpChanger):
    // a move, OpenPnP's First (1) to Last (4) Location; an actuator, its Post
    // 1 to 3 Actuator. 0: a step of jplacer's own.
    int                   openPnpSlot = 0;

    static const char* kindName(Kind k) {
        switch (k) {
            case Kind::Move:     return "move";
            case Kind::SafeZ:    return "safe Z";
            case Kind::Actuator: return "actuator";
            case Kind::Wait:     return "wait";
            case Kind::Ask:      return "ask";
        }
        return "move";
    }

    static JPChangerStep fromJson(const JJson& j) {
        JPChangerStep s;
        const std::string& k = j["kind"].str();
        for (Kind c : { Kind::Move, Kind::SafeZ, Kind::Actuator, Kind::Wait, Kind::Ask })
            if (k == kindName(c)) s.kind = c;
        auto coordinate = [&j](const char* key) -> std::optional<double> {
            if (!j[key].isNumber()) return std::nullopt;
            return j[key].number();
        };
        s.x          = coordinate("x");
        s.y          = coordinate("y");
        s.z          = coordinate("z");
        s.rotation   = coordinate("rotation");
        s.speed      = j["speed"].number(1.0);
        s.actuatorId = j["actuator"].str();
        s.on         = j["on"].boolean(true);
        s.waitMs     = int(j["waitMs"].number());
        s.message    = j["message"].str();
        s.openPnpSlot = int(j["openPnpSlot"].number(0));
        return s;
    }
    // Only what its kind uses.
    JJson toJson() const {
        JJson j = JJson::object();
        j["kind"] = kindName(kind);
        switch (kind) {
            case Kind::Move:
                if (x)        j["x"] = *x;
                if (y)        j["y"] = *y;
                if (z)        j["z"] = *z;
                if (rotation) j["rotation"] = *rotation;
                j["speed"] = speed;
                break;
            case Kind::SafeZ:    j["speed"] = speed; break;
            case Kind::Actuator: j["actuator"] = actuatorId; j["on"] = on; break;
            case Kind::Wait:     j["waitMs"] = waitMs; break;
            case Kind::Ask:      j["message"] = message; break;
        }
        if (openPnpSlot) j["openPnpSlot"] = openPnpSlot;
        return j;
    }
};

} // inline namespace jf
