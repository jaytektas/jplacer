// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPOffsetsRun.h"

#include <algorithm>
#include <cmath>

inline namespace jf {

double JPOffsetsRun::change() const { return std::hypot(changeX(), changeY()); }

double JPOffsetsRun::distance(const Angle& a) const { return std::hypot(a.dx - changeX(), a.dy - changeY()); }

double JPOffsetsRun::spread() const {
    double most = 0;
    for (const Angle& a : angles) most = std::max(most, distance(a));
    return most;
}

double JPOffsetsRun::outlierLimit() const {
    if (angles.empty()) return kLeastOutlierMm;
    std::vector<double> d;
    for (const Angle& a : angles) d.push_back(distance(a));
    std::sort(d.begin(), d.end());
    const size_t n = d.size();
    const double median = n % 2 ? d[n / 2] : (d[n / 2 - 1] + d[n / 2]) / 2;
    return std::max(kLeastOutlierMm, kOutlierFactor * median);
}

int JPOffsetsRun::outliers() const {
    return int(std::count_if(angles.begin(), angles.end(), [this](const Angle& a) { return outlier(a); }));
}

JPOffsetsRun JPOffsetsRun::fromJson(const JJson& j) {
    JPOffsetsRun r;
    r.when    = j["when"].str();
    r.how     = j["how"].str();
    r.beforeX = j["before"]["x"].number();
    r.beforeY = j["before"]["y"].number();
    r.afterX  = j["after"]["x"].number();
    r.afterY  = j["after"]["y"].number();
    for (const JJson& a : j["angles"].arr()) r.angles.push_back({ a["angle"].number(), a["dx"].number(), a["dy"].number() });
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
    if (!angles.empty()) {
        JJson list = JJson::array();
        for (const Angle& a : angles) {
            JJson o = JJson::object();
            o["angle"] = a.angle;
            o["dx"] = a.dx;
            o["dy"] = a.dy;
            list.push(o);
        }
        j["angles"] = list;
    }
    return j;
}

} // inline namespace jf
