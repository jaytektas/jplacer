// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include <j/config/Json.h>

#include <string>
#include <utility>
#include <vector>

inline namespace jf {

// What measuring an axis's backlash found (JPBacklashCalibrator), kept with
// the axis for its graphs: the play measured against how far each move came
// in from the other side, against speed, and the errors left once
// compensated, each move coming in from somewhere at random. Millimetres
// (degrees on a rotation axis).
struct JPBacklashCalibration {
    using Points = std::vector<std::pair<double, double>>;   // (x, y)

    std::string when;
    double      toleranceMm = 0;    // from the measuring's own noise
    Points      byDistance;         // (distance come in from the other side, play)
    Points      bySpeed;            // (speed factor, play)
    Points      after;              // (signed distance come in from, error)

    static Points pointsFrom(const JJson& j) {
        Points out;
        for (const JJson& p : j.arr()) out.push_back({ p[0].number(), p[1].number() });
        return out;
    }
    static JJson pointsJson(const Points& ps) {
        JJson j = JJson::array();
        for (const auto& [x, y] : ps) {
            JJson p = JJson::array();
            p.push(JJson(x));
            p.push(JJson(y));
            j.push(p);
        }
        return j;
    }
    static JPBacklashCalibration fromJson(const JJson& j) {
        JPBacklashCalibration c;
        c.when = j["when"].str();
        c.toleranceMm = j["tolerance"].number();
        c.byDistance = pointsFrom(j["byDistance"]);
        c.bySpeed = pointsFrom(j["bySpeed"]);
        c.after = pointsFrom(j["after"]);
        return c;
    }
    JJson toJson() const {
        JJson j = JJson::object();
        j["when"] = when;
        j["tolerance"] = toleranceMm;
        j["byDistance"] = pointsJson(byDistance);
        j["bySpeed"] = pointsJson(bySpeed);
        j["after"] = pointsJson(after);
        return j;
    }
};

} // inline namespace jf
