// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPVacuumMeasure.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>

inline namespace jf {

namespace {

std::string number(double v) {
    char text[32];
    std::snprintf(text, sizeof text, "%.1f", v);
    return text;
}

std::optional<JPVacuumMeasure::Stats> statsIn(const JJson& j) {
    if (!j.isObject()) return std::nullopt;
    return JPVacuumMeasure::Stats::fromJson(j);
}

} // namespace

JPVacuumMeasure::Stats JPVacuumMeasure::Stats::of(const std::vector<double>& values) {
    Stats s;
    s.count = int(values.size());
    if (values.empty()) return s;
    double sum = 0;
    for (double v : values) sum += v;
    s.mean = sum / double(values.size());
    double squares = 0;
    for (double v : values) squares += (v - s.mean) * (v - s.mean);
    s.sd = values.size() > 1 ? std::sqrt(squares / double(values.size() - 1)) : 0;
    std::vector<double> sorted = values;
    std::sort(sorted.begin(), sorted.end());
    s.lowest = sorted.front();
    s.highest = sorted.back();
    double step = std::numeric_limits<double>::infinity();
    for (size_t i = 1; i < sorted.size(); ++i)
        if (sorted[i] > sorted[i - 1]) step = std::min(step, sorted[i] - sorted[i - 1]);
    if (std::isinf(step)) {
        // All the same: whole numbers step by 1; otherwise a thousandth of the level.
        const bool whole = sorted.front() == std::floor(sorted.front());
        step = whole || s.mean == 0 ? 1 : std::abs(s.mean) * 1e-3;
    }
    s.resolution = step;
    return s;
}

double JPVacuumMeasure::Stats::sigma() const { return std::max(sd, resolution / 2); }

double JPVacuumMeasure::Stats::halfWidth() const { return std::max({ kSigmas * sigma(), highest - lowest, resolution }); }

JPVacuumMeasure::Stats JPVacuumMeasure::Stats::fromJson(const JJson& j) {
    Stats s;
    s.count      = int(j["count"].number());
    s.mean       = j["mean"].number();
    s.sd         = j["sd"].number();
    s.lowest     = j["lowest"].number();
    s.highest    = j["highest"].number();
    s.resolution = j["resolution"].number();
    return s;
}

JJson JPVacuumMeasure::Stats::toJson() const {
    JJson j = JJson::object();
    j["count"] = count;
    j["mean"] = mean;
    j["sd"] = sd;
    j["lowest"] = lowest;
    j["highest"] = highest;
    j["resolution"] = resolution;
    return j;
}

JPVacuumMeasure::Range JPVacuumMeasure::around(const Stats& s, const std::optional<Stats>& other) {
    const double h = s.halfWidth();
    Range r { s.mean - h, s.mean + h, false };
    if (!other || other->count == 0 || other->mean == s.mean) return r;
    // Where the two are as many of their own deviations away from each.
    const double a = s.sigma(), b = other->sigma();
    const double boundary = (s.mean * b + other->mean * a) / (a + b);
    if (other->mean > s.mean && r.high > boundary) {
        r.high = boundary;
        r.stopped = true;
    } else if (other->mean < s.mean && r.low < boundary) {
        r.low = boundary;
        r.stopped = true;
    }
    return r;
}

double JPVacuumMeasure::separation(const Stats& a, const Stats& b) {
    return std::abs(a.mean - b.mean) / (a.sigma() + b.sigma());
}

JPVacuumMeasure::Ranges JPVacuumMeasure::ranges(bool partOn, const std::string& method) const {
    Ranges out;
    out.method = method == "None" || method.empty() ? "Absolute" : method;
    const bool difference = out.method == "Difference";
    if (!partOn) {
        // Part off: the pulse with nothing on the nozzle; anything else (a part, the nozzle blocked) is not off.
        if (level.count == 0) return out;
        out.vacuum = around(difference ? baseline : level, std::nullopt);
        out.difference = around(this->difference, std::nullopt);
        return out;
    }
    if (!held()) {
        out.notes.push_back("Measure With Part to set the ranges: so far only the level without a part is known.");
        return out;
    }
    out.vacuum = around(level, open);
    // A part lost while held: the level goes to the one without a part, the difference with it.
    std::optional<Stats> lost;
    if (open) {
        Stats l;
        l.count = open->count;
        l.mean = open->mean - baseline.mean;
        l.sd = std::hypot(open->sigma(), baseline.sigma());
        l.resolution = open->resolution;
        lost = l;
    }
    out.difference = around(this->difference, lost);
    if (!open) {
        out.notes.push_back("Not measured without a part: the range is not kept off the level an empty nozzle reads. "
                            "Measure Without Part too.");
    } else if (const double apart = separation(level, *open); apart < kSigmas) {
        // A range that takes in the level without a part would pass a nozzle that has lost it: none set.
        out.vacuum.reset();
        out.difference.reset();
        out.notes.push_back("With a part the vacuum reads " + number(level.mean) + " (deviation " + number(level.sd)
                            + "), without " + number(open->mean) + " (deviation " + number(open->sd) + "): only "
                            + number(apart) + " deviations apart, so a check could take one for the other, and the "
                            "ranges are left as they were. A tip that seals better on the part, or more vacuum, sets "
                            "them further apart.");
    }
    return out;
}

JPVacuumMeasure JPVacuumMeasure::fromJson(const JJson& j) {
    JPVacuumMeasure m;
    m.when = j["when"].str();
    if (const auto s = statsIn(j["baseline"])) m.baseline = *s;
    if (const auto s = statsIn(j["level"])) m.level = *s;
    if (const auto s = statsIn(j["difference"])) m.difference = *s;
    m.open = statsIn(j["open"]);
    return m;
}

JJson JPVacuumMeasure::toJson() const {
    JJson j = JJson::object();
    j["when"] = when;
    if (baseline.count) j["baseline"] = baseline.toJson();
    if (level.count) j["level"] = level.toJson();
    if (difference.count) j["difference"] = difference.toJson();
    if (open) j["open"] = open->toJson();
    return j;
}

} // inline namespace jf
