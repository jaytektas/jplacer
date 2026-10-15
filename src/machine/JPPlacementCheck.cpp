// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPPlacementCheck.h"

#include <algorithm>
#include <cmath>

inline namespace jf {

double JPPlacementCheck::biasX() const {
    double s = 0;
    for (const Place& p : places) s += p.errorX;
    return places.empty() ? 0 : s / double(places.size());
}

double JPPlacementCheck::biasY() const {
    double s = 0;
    for (const Place& p : places) s += p.errorY;
    return places.empty() ? 0 : s / double(places.size());
}

void JPPlacementCheck::turning(double& x, double& y) const {
    // Each error less the bias, turned back by its angle: the turning part's estimate there; their mean.
    x = y = 0;
    if (places.empty()) return;
    const double bx = biasX(), by = biasY();
    for (const Place& p : places) {
        const double a = -p.angle * M_PI / 180, ex = p.errorX - bx, ey = p.errorY - by;
        x += ex * std::cos(a) - ey * std::sin(a);
        y += ex * std::sin(a) + ey * std::cos(a);
    }
    x /= double(places.size());
    y /= double(places.size());
}

double JPPlacementCheck::residual(const Place& p) const {
    double tx = 0, ty = 0;
    turning(tx, ty);
    const double a = p.angle * M_PI / 180;
    const double rx = p.errorX - biasX() - (tx * std::cos(a) - ty * std::sin(a));
    const double ry = p.errorY - biasY() - (tx * std::sin(a) + ty * std::cos(a));
    return std::hypot(rx, ry);
}

double JPPlacementCheck::worstResidual() const {
    double most = 0;
    for (const Place& p : places) most = std::max(most, residual(p));
    return most;
}

double JPPlacementCheck::driftX() const {
    double s = 0;
    for (const Drift& d : drifts) s += d.dx;
    return drifts.empty() ? 0 : s / double(drifts.size());
}

double JPPlacementCheck::driftY() const {
    double s = 0;
    for (const Drift& d : drifts) s += d.dy;
    return drifts.empty() ? 0 : s / double(drifts.size());
}

double JPPlacementCheck::visionBiasX() const {
    double s = 0;
    for (const Place& p : places) s += p.offsetX;
    return places.empty() ? 0 : s / double(places.size());
}

double JPPlacementCheck::visionBiasY() const {
    double s = 0;
    for (const Place& p : places) s += p.offsetY;
    return places.empty() ? 0 : s / double(places.size());
}

JPPlacementCheck JPPlacementCheck::fromJson(const JJson& j) {
    JPPlacementCheck c;
    c.when = j["when"].str();
    c.z = j["z"].number();
    for (const JJson& d : j["drifts"].arr()) c.drifts.push_back({ d["x"].number(), d["y"].number() });
    for (const JJson& p : j["places"].arr())
        c.places.push_back({ p["angle"].number(), p["offset"]["x"].number(), p["offset"]["y"].number(), p["error"]["x"].number(),
                             p["error"]["y"].number() });
    return c;
}

JJson JPPlacementCheck::toJson() const {
    JJson j = JJson::object();
    j["when"] = when;
    j["z"] = z;
    j["drifts"] = JJson::array();
    for (const Drift& d : drifts) {
        JJson o = JJson::object();
        o["x"] = d.dx;
        o["y"] = d.dy;
        j["drifts"].push(o);
    }
    j["places"] = JJson::array();
    for (const Place& p : places) {
        JJson o = JJson::object();
        o["angle"] = p.angle;
        o["offset"]["x"] = p.offsetX;
        o["offset"]["y"] = p.offsetY;
        o["error"]["x"] = p.errorX;
        o["error"]["y"] = p.errorY;
        j["places"].push(o);
    }
    return j;
}

} // inline namespace jf
