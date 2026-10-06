// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPMachineLocation.h"

#include <j/config/Json.h>

#include <array>
#include <optional>

inline namespace jf {

// OpenPnP's ReferenceAdvancedMotionPlanner, as jplacer runs it.
//
// Continuous motion: a move does not wait for the machine to stand still;
// the cell waits only when it must (an actuator coordinating with the
// machine, a nozzle picking or placing, each piece of work's end), so the
// moves of one operation (up to safe Z, over, down) go to the controller
// back to back.
//
// Test Motion: the selected tool through up to four places (each Enabled?),
// at a speed between each two (a fraction of the machine's) and by way of
// safe Z or straight; run forward from the first, or back from the last when
// the tool is nearer that one. Diagnostics: the run's time, as planned from
// the axes' feed rates and accelerations and as it took, and where each axis
// was over the run, from the controllers' reports.
struct JPMotionPlannerConfig {
    struct Stop {
        bool              enabled = false;
        JPMachineLocation at;
    };

    bool                continuousMotion = false;
    bool                diagnosticsEnabled = false;
    // OpenPnP's Minimum Speed: the slowest share of full speed offered (the Jog panel's speed goes no lower).
    double              minimumSpeed = kDefaultMinimumSpeed;
    // OpenPnP's Interpolation Retiming?: interpolated moves (Simulated3rdOrderControl) stretched to the planned time.
    bool                interpolationRetiming = true;
    static constexpr double kDefaultMinimumSpeed = 0.05;
    std::array<Stop, 4> stops { Stop{ false, { 0, 0, 0, 0 } }, Stop{ false, { 0, 400, 0, 0 } },
                                Stop{ false, { 380, 400, 0, 0 } }, Stop{ false, { 380, 0, 0, 0 } } };
    std::array<double, 3> speeds { 1, 1, 1 };   // between stop i and i+1
    std::array<bool, 3>   safeZ { true, true, true };

    // The stop a run starts from: the first enabled of the first three
    // (forward), or of the last three from the end (reverse); none without.
    std::optional<int> initial(bool reverse) const {
        if (reverse) {
            for (int i = 3; i >= 1; --i)
                if (stops[size_t(i)].enabled) return i;
        } else {
            for (int i = 0; i <= 2; ++i)
                if (stops[size_t(i)].enabled) return i;
        }
        return std::nullopt;
    }

    static JPMotionPlannerConfig fromJson(const JJson& j) {
        JPMotionPlannerConfig p;
        if (!j.isObject()) return p;
        p.continuousMotion   = j["continuousMotion"].boolean(false);
        p.diagnosticsEnabled = j["diagnosticsEnabled"].boolean(false);
        p.minimumSpeed = j["minimumSpeed"].number(kDefaultMinimumSpeed);
        p.interpolationRetiming = j["interpolationRetiming"].boolean(true);
        for (size_t i = 0; i < p.stops.size(); ++i) {
            const JJson& s = j["testMotion"][i];
            if (!s.isObject()) continue;
            p.stops[i].enabled = s["enabled"].boolean(false);
            p.stops[i].at = JPMachineLocation::fromJson(s["at"]).value_or(p.stops[i].at);
            if (i < 3) {
                p.speeds[i] = s["speed"].number(1.0);
                p.safeZ[i]  = s["safeZ"].boolean(true);
            }
        }
        return p;
    }
    JJson toJson() const {
        JJson j = JJson::object();
        j["continuousMotion"] = continuousMotion;
        j["diagnosticsEnabled"] = diagnosticsEnabled;
        j["minimumSpeed"] = minimumSpeed;
        j["interpolationRetiming"] = interpolationRetiming;
        JJson stopsJ = JJson::array();
        for (size_t i = 0; i < stops.size(); ++i) {
            JJson s = JJson::object();
            s["enabled"] = stops[i].enabled;
            s["at"] = stops[i].at.toJson();
            if (i < 3) {
                s["speed"] = speeds[i];
                s["safeZ"] = safeZ[i];
            }
            stopsJ.push(s);
        }
        j["testMotion"] = stopsJ;
        return j;
    }
};

} // inline namespace jf
