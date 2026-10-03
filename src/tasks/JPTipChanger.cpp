// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPTipChanger.h"

#include "common/JPlacerLog.h"

#include <j/core/Log.h>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <thread>

inline namespace jf {

namespace {

std::string number(double v) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.3f", v);
    std::string s = buf;
    while (!s.empty() && s.back() == '0') s.pop_back();
    if (!s.empty() && s.back() == '.') s.pop_back();
    return s;
}

std::string percent(double share) {
    return number(share * 100) + "%";
}

} // namespace

std::string JPTipChanger::describe(const JPCellConfig& cell, const JPChangerStep& s) {
    using K = JPChangerStep::Kind;
    switch (s.kind) {
        case K::Move: {
            std::string at;
            if (s.x) at += " X " + number(*s.x);
            if (s.y) at += " Y " + number(*s.y);
            if (s.z) at += " Z " + number(*s.z);
            if (s.rotation) at += " C " + number(*s.rotation);
            return "move to" + at + " at " + percent(s.speed);
        }
        case K::SafeZ: return "up to safe Z at " + percent(s.speed);
        case K::Actuator: {
            std::string name = s.actuatorId;
            for (const JPActuatorConfig& a : cell.actuators) if (a.id == s.actuatorId) name = a.name;
            return "switch " + name + (s.on ? " on" : " off");
        }
        case K::Wait: return "wait " + std::to_string(s.waitMs) + " ms";
        case K::Ask: return "ask: " + s.message;
    }
    return "";
}

bool JPTipChanger::run(JPCell& cell, const JPCellConfig& names, const JPNozzleConfig& nozzle,
                       const std::vector<JPChangerStep>& steps, const std::string& what, bool everyStep,
                       const Hooks& hooks, std::string& why) {
    using K = JPChangerStep::Kind;
    const JPMountConfig& m = nozzle.mount;
    // A place for the nozzle, as its axes' targets: the nozzle's offset taken off.
    auto targets = [&m](const JPChangerStep& s, bool withZ) {
        std::map<std::string, double> t;
        if (s.x && !m.axisX.empty()) t[m.axisX] = *s.x - m.offsetX;
        if (s.y && !m.axisY.empty()) t[m.axisY] = *s.y - m.offsetY;
        if (s.rotation && !m.axisRotation.empty()) t[m.axisRotation] = *s.rotation;
        if (withZ && s.z && !m.axisZ.empty()) t[m.axisZ] = *s.z - m.offsetZ;
        return t;
    };
    auto say = [&](const std::string& text) {
        JLOGC(JPlacerLog::kCell, JLogLevel::Info) << what << ": " << text;
        if (hooks.progress) hooks.progress(text);
    };
    auto moveTo = [&](std::map<std::string, double> t, double speed) {
        return t.empty() || cell.moveAxesAndWait(std::move(t), speed, why, /*squared=*/false);
    };

    bool firstMove = true;
    for (size_t i = 0; i < steps.size(); ++i) {
        const JPChangerStep& s = steps[i];
        const std::string step = "step " + std::to_string(i + 1) + " of " + std::to_string(steps.size()) + ": "
                               + describe(names, s);
        if (s.kind == K::Ask) {
            if (!hooks.ask || !hooks.ask(what + ", " + step)) {
                why = "stopped at " + step;
                return false;
            }
            continue;
        }
        if (everyStep && (!hooks.ask || !hooks.ask(what + ", " + step + "?"))) {
            why = "stopped before " + step;
            return false;
        }
        say(step);
        bool ok = true;
        switch (s.kind) {
            case K::Move:
                if (firstMove) {
                    // In from safe Z: up, across, then down.
                    ok = cell.safeZAndWait(m.headId, s.speed, why) && moveTo(targets(s, false), s.speed)
                      && (!s.z || moveTo({ { m.axisZ, *s.z - m.offsetZ } }, s.speed));
                    firstMove = false;
                } else {
                    ok = moveTo(targets(s, true), s.speed);
                }
                break;
            case K::SafeZ:
                ok = cell.safeZAndWait(m.headId, s.speed, why);
                break;
            case K::Actuator:
                ok = cell.switchActuatorAndWait(s.actuatorId, s.on, why);
                break;
            case K::Wait:
                std::this_thread::sleep_for(std::chrono::milliseconds(s.waitMs));
                break;
            case K::Ask:
                break;
        }
        if (!ok) {
            why = step + " failed: " + why;
            return false;
        }
    }
    // Done: the head up out of the changer.
    say("up to safe Z");
    if (!cell.safeZAndWait(m.headId, 1.0, why)) {
        why = "going up to safe Z at the end failed: " + why;
        return false;
    }
    return true;
}

} // inline namespace jf
