// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPOffsetsRun.h"

#include <algorithm>
#include <cmath>

inline namespace jf {

double JPOffsetsRun::Height::meanX() const {
    double s = 0;
    for (const Angle& a : angles) s += a.dx;
    return angles.empty() ? 0 : s / double(angles.size());
}

double JPOffsetsRun::Height::meanY() const {
    double s = 0;
    for (const Angle& a : angles) s += a.dy;
    return angles.empty() ? 0 : s / double(angles.size());
}

double JPOffsetsRun::Height::distance(const Angle& a) const { return std::hypot(a.dx - meanX(), a.dy - meanY()); }

double JPOffsetsRun::Height::spread() const {
    double most = 0;
    for (const Angle& a : angles) most = std::max(most, distance(a));
    return most;
}

double JPOffsetsRun::Height::outlierLimit() const {
    if (angles.empty()) return kLeastOutlierMm;
    std::vector<double> d;
    for (const Angle& a : angles) d.push_back(distance(a));
    std::sort(d.begin(), d.end());
    const size_t n = d.size();
    const double median = n % 2 ? d[n / 2] : (d[n / 2 - 1] + d[n / 2]) / 2;
    return std::max(kLeastOutlierMm, kOutlierFactor * median);
}

int JPOffsetsRun::Height::outliers() const {
    return int(std::count_if(angles.begin(), angles.end(), [this](const Angle& a) { return outlier(a); }));
}

double JPOffsetsRun::change() const { return std::hypot(changeX(), changeY()); }

double JPOffsetsRun::spread() const { return std::max(first.spread(), second ? second->spread() : 0.0); }

int JPOffsetsRun::outliers() const { return first.outliers() + (second ? second->outliers() : 0); }

namespace {

std::vector<JPOffsetsRun::Angle> anglesOf(const JJson& list) {
    std::vector<JPOffsetsRun::Angle> out;
    for (const JJson& a : list.arr()) out.push_back({ a["angle"].number(), a["dx"].number(), a["dy"].number() });
    return out;
}

JJson anglesJson(const std::vector<JPOffsetsRun::Angle>& angles) {
    JJson list = JJson::array();
    for (const JPOffsetsRun::Angle& a : angles) {
        JJson o = JJson::object();
        o["angle"] = a.angle;
        o["dx"] = a.dx;
        o["dy"] = a.dy;
        list.push(o);
    }
    return list;
}

} // namespace

JPOffsetsRun JPOffsetsRun::fromJson(const JJson& j) {
    JPOffsetsRun r;
    r.when         = j["when"].str();
    r.how          = j["how"].str();
    r.beforeX      = j["before"]["x"].number();
    r.beforeY      = j["before"]["y"].number();
    r.afterX       = j["after"]["x"].number();
    r.afterY       = j["after"]["y"].number();
    r.first.z      = j["z"].number();
    r.first.angles = anglesOf(j["angles"]);
    if (j["second"].isObject()) r.second = Height { j["second"]["z"].number(), anglesOf(j["second"]["angles"]) };
    r.leanX        = j["lean"]["x"].number();
    r.leanY        = j["lean"]["y"].number();
    return r;
}

JJson JPOffsetsRun::toJson() const {
    JJson j = JJson::object();
    j["when"] = when;
    j["how"] = how;
    j["before"]["x"] = beforeX;
    j["before"]["y"] = beforeY;
    j["after"]["x"] = afterX;
    j["after"]["y"] = afterY;
    if (!first.angles.empty()) {
        j["z"] = first.z;
        j["angles"] = anglesJson(first.angles);
    }
    if (second) {
        j["second"]["z"] = second->z;
        j["second"]["angles"] = anglesJson(second->angles);
    }
    if (leanX != 0 || leanY != 0) {
        j["lean"]["x"] = leanX;
        j["lean"]["y"] = leanY;
    }
    return j;
}

} // inline namespace jf
