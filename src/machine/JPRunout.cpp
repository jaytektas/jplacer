// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPRunout.h"

#include <cmath>

inline namespace jf {

namespace {

constexpr double kRad = M_PI / 180;

} // namespace

const std::vector<std::string>& JPRunout::algorithms() {
    static const std::vector<std::string> names { "Model", "ModelAffine", "ModelNoOffset", "ModelNoOffsetAffine",
                                                  "ModelCameraOffset", "ModelCameraOffsetAffine", "Table" };
    return names;
}

void JPRunout::runoutAt(double a, double& dx, double& dy) const {
    const double t = (a - phaseDeg) * kRad;
    dx = radius * std::cos(t);
    dy = radius * std::sin(t);
}

void JPRunout::offset(double a, double& dx, double& dy) const {
    if (table()) {
        // OpenPnP's TableBasedRunoutCompensation: between the two measurements the angle lies between (past the
        // last, between it and the first), in proportion.
        dx = dy = 0;
        if (points.empty()) return;
        while (a < -180) a += 360;
        while (a > 180) a -= 360;
        const Point* p = nullptr;
        const Point* q = nullptr;
        if (a >= points.back().angle) {
            p = &points.back();
            q = &points.front();
        } else {
            for (size_t i = 0; i + 1 < points.size() && !p; ++i)
                if (a < points[i + 1].angle) {
                    p = &points[i];
                    q = &points[i + 1];
                }
        }
        if (!p) {
            p = q = &points.front();
        }
        const double ratio = q->angle - p->angle != 0 ? (a - p->angle) / (q->angle - p->angle) : 1.0;
        dx = p->dx + (q->dx - p->dx) * ratio;
        dy = p->dy + (q->dy - p->dy) * ratio;
        return;
    }
    runoutAt(a, dx, dy);
    if (algorithm == "Model" || algorithm == "ModelAffine") {
        dx += centreX;
        dy += centreY;
    }
}

void JPRunout::cameraOffset(double& dx, double& dy) const {
    const bool camera = algorithm == "ModelCameraOffset" || algorithm == "ModelCameraOffsetAffine";
    dx = camera ? centreX : 0;
    dy = camera ? centreY : 0;
}

void JPRunout::estimateError() {
    // OpenPnP's estimateModelError: each measurement against the model (its centre and swing).
    rmsMm = peakMm = 0;
    if (points.empty() || table()) return;
    double sum = 0;
    for (const Point& p : points) {
        double sx, sy;
        runoutAt(p.angle, sx, sy);
        const double e = std::hypot(p.dx - centreX - sx, p.dy - centreY - sy);
        sum += e * e;
        peakMm = std::max(peakMm, e);
    }
    rmsMm = std::sqrt(sum / double(points.size()));
}

JPRunout JPRunout::fromJson(const JJson& j) {
    JPRunout r;
    r.algorithm = j["algorithm"].isString() ? j["algorithm"].str() : std::string(kKeptAlgorithm);
    r.centreX = j["centre"]["x"].number();
    r.centreY = j["centre"]["y"].number();
    r.radius = j["radius"].number();
    r.phaseDeg = j["phase"].number();
    r.rmsMm = j["rms"].number();
    r.peakMm = j["peak"].number();
    r.when = j["when"].str();
    for (const JJson& p : j["points"].arr()) r.points.push_back({ p[0].number(), p[1].number(), p[2].number() });
    return r;
}

JJson JPRunout::toJson() const {
    JJson j = JJson::object();
    j["algorithm"] = algorithm;
    j["centre"]["x"] = centreX;
    j["centre"]["y"] = centreY;
    j["radius"] = radius;
    j["phase"] = phaseDeg;
    j["rms"] = rmsMm;
    j["peak"] = peakMm;
    j["when"] = when;
    j["points"] = JJson::array();
    for (const Point& p : points) {
        JJson q = JJson::array();
        q.push(JJson(p.angle));
        q.push(JJson(p.dx));
        q.push(JJson(p.dy));
        j["points"].push(q);
    }
    return j;
}

} // inline namespace jf
