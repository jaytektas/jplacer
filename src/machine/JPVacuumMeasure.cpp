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

void JPVacuumMeasure::take(const Readings& r, bool partOn, bool withPart) {
    auto from = [](const std::vector<double>& v, const std::vector<double>& base) {
        std::vector<double> out;
        for (size_t i = 0; i < v.size(); ++i) out.push_back(v[i] - base[std::min(i, base.size() - 1)]);
        return out;
    };
    if (partOn && !withPart) {
        open = Stats::of(r.during);
        return;
    }
    baseline = Stats::of(r.before);
    level = Stats::of(r.during);
    difference = Stats::of(r.before.empty() ? std::vector<double>() : from(r.during, r.before));
    if (!partOn && !r.after.empty() && !r.before.empty()) {
        settled = Stats::of(r.after);
        settledDifference = Stats::of(from(r.after, r.before));
    } else {
        settled.reset();
        settledDifference.reset();
    }
}

std::optional<JPVacuumMeasure::Range> JPVacuumMeasure::around(const Stats& s, const std::optional<Stats>& other) {
    if (!other || other->count == 0) {
        const double h = s.halfWidth();
        return Range { s.mean - h, s.mean + h, false };
    }
    if (separation(s, *other) < kSigmas) return std::nullopt;
    // Where the two are as many of their own deviations away from each; away from the other, as far again as the
    // level itself (to 0, or twice it), at least the boundary's distance: further that way is no risk.
    const double a = s.sigma(), b = other->sigma();
    const double boundary = (s.mean * b + other->mean * a) / (a + b);
    const double away = s.mean > boundary ? 1 : -1;
    const double far = s.mean + away * std::max(std::abs(s.mean - boundary), std::abs(s.mean));
    return Range { std::min(boundary, far), std::max(boundary, far), true };
}

double JPVacuumMeasure::separation(const Stats& a, const Stats& b) {
    return std::abs(a.mean - b.mean) / (a.sigma() + b.sigma());
}

namespace {

std::string told(const JPVacuumMeasure::Stats& s) {
    return number(s.mean) + " (deviation " + number(s.sd) + ")";
}

std::string tooClose(const std::string& what, const JPVacuumMeasure::Stats& a, const std::string& other,
                     const JPVacuumMeasure::Stats& b) {
    return what + " reads " + told(a) + ", " + other + " " + told(b) + ": only " + number(JPVacuumMeasure::separation(a, b))
         + " deviations apart, so a check could take one for the other, and the ranges are left as they were.";
}

// A difference's level: `a` less `b`, their deviations together.
JPVacuumMeasure::Stats less(const JPVacuumMeasure::Stats& a, const JPVacuumMeasure::Stats& b) {
    JPVacuumMeasure::Stats d;
    d.count = std::min(a.count, b.count);
    d.mean = a.mean - b.mean;
    d.sd = std::hypot(a.sigma(), b.sigma());
    d.resolution = std::max(a.resolution, b.resolution);
    return d;
}

} // namespace

JPVacuumMeasure::Ranges JPVacuumMeasure::ranges(bool partOn, const std::string& method, int dwellMs,
                                                const JPVacuumMeasure* partner) const {
    Ranges out = rangesBy(partOn, method, dwellMs, partner);
    if (!out.tooClose) return out;
    // The other method, where it tells them apart.
    Ranges other = rangesBy(partOn, out.method == "Difference" ? "Absolute" : "Difference", dwellMs, partner);
    if (other.tooClose || !other.vacuum) return out;
    std::vector<std::string> notes;
    for (const std::string& n : out.notes) notes.push_back(n == out.notes.back() ? n.substr(0, n.rfind(", and the ranges")) + "." : n);
    notes.push_back("Measured as " + other.method + ", they are told apart: Measurement Method set to " + other.method + ".");
    for (const std::string& n : other.notes)
        if (std::find(notes.begin(), notes.end(), n) == notes.end()) notes.push_back(n);
    other.notes = notes;
    return other;
}

JPVacuumMeasure::Ranges JPVacuumMeasure::rangesBy(bool partOn, const std::string& method, int dwellMs,
                                                  const JPVacuumMeasure* partner) const {
    Ranges out;
    out.method = method == "None" || method.empty() ? "Absolute" : method;
    const bool byDifference = out.method == "Difference";
    if (!partOn) {
        if (!held()) return out;
        if (dwellMs > 0 && !settled) {
            out.notes.push_back("Measured with no Valve close wait: Measure again for the wait now set.");
            return out;
        }
        bool afterClose = dwellMs > 0;
        if (afterClose) {
            // Back most of the way to the baseline as the valve closed: the sensor reads the pump, not the nozzle.
            const double dip = baseline.mean - level.mean;
            const double back = dip == 0 ? 1 : (settled->mean - level.mean) / dip;
            if (back > kRecovered) {
                out.dwellMs = 0;
                afterClose = false;
                out.notes.push_back("As the valve closed the level went " + number(back * 100) + "% of the way back to "
                                    "where it was before the pulse (" + told(*settled) + " against " + told(baseline)
                                    + "), whatever was on the nozzle: the sensor reads the pump's side of the valve. The "
                                    "check reads the level as the valve closes instead (" + told(level)
                                    + "): Valve close set to 0.");
            }
        }
        const Stats& reading = afterClose ? *settled : level;
        const Stats& change = afterClose ? *settledDifference : difference;
        std::optional<Stats> blocked, blockedChange, pumpDown;
        if (partner && partner->held() && !afterClose) {
            blocked = partner->level;
            blockedChange = less(partner->level, baseline);
        }
        if (partner && partner->open) pumpDown = partner->open;
        if (!blocked)
            out.notes.push_back(afterClose
                ? std::string("The check reads the level after the Valve close wait, where a blocked nozzle's level is not "
                              "measured: the ranges are only these readings' spread, which the pump's drift can leave.")
                : std::string("Measure Part On With Part too: a held part's level is a blocked nozzle's, which Part "
                              "Off's range is told from. Until then it is only these readings' spread, which the pump's "
                              "drift can leave."));
        out.vacuum = byDifference ? around(baseline, pumpDown) : around(reading, blocked);
        out.difference = around(change, blockedChange);
        // Only what the method checks need be told apart.
        if (!out.vacuum || (byDifference && !out.difference)) {
            const bool vacuumTold = out.vacuum.has_value();
            out.tooClose = true;
            out.vacuum.reset();
            out.difference.reset();
            if (vacuumTold)
                out.notes.push_back(tooClose("The pulse's change with nothing on the nozzle", change, "with it blocked", *blockedChange));
            else if (byDifference)
                out.notes.push_back(tooClose("The pump before the pulse", baseline, "pulled down by an open nozzle", *pumpDown));
            else
                out.notes.push_back(tooClose("With nothing on the nozzle the pulse", reading, "with it blocked", *blocked));
        }
        return out;
    }
    if (!held()) {
        out.notes.push_back("Measure With Part to set the ranges: so far only the level without a part is known.");
        return out;
    }
    // A part lost while held: the level goes to the one without a part, the difference with it.
    std::optional<Stats> lost;
    if (open) lost = less(*open, baseline);
    out.vacuum = around(level, open);
    out.difference = around(difference, lost);
    if (!open) {
        out.notes.push_back("Not measured without a part: the range is not told from the level an empty nozzle reads, "
                            "only these readings' spread. Measure Without Part too.");
    } else if (!out.vacuum || (byDifference && !out.difference)) {
        // A range taking in the level without a part would pass a nozzle that has lost it: none set.
        out.tooClose = true;
        out.vacuum.reset();
        out.difference.reset();
        out.notes.push_back(tooClose("With a part the vacuum", level, "without", *open)
                            + " A tip that seals better on the part, or more vacuum, sets them further apart.");
    }
    return out;
}

JPVacuumMeasure JPVacuumMeasure::fromJson(const JJson& j) {
    JPVacuumMeasure m;
    m.when = j["when"].str();
    if (const auto s = statsIn(j["baseline"])) m.baseline = *s;
    if (const auto s = statsIn(j["level"])) m.level = *s;
    if (const auto s = statsIn(j["difference"])) m.difference = *s;
    m.settled = statsIn(j["settled"]);
    m.settledDifference = statsIn(j["settledDifference"]);
    m.open = statsIn(j["open"]);
    return m;
}

JJson JPVacuumMeasure::toJson() const {
    JJson j = JJson::object();
    j["when"] = when;
    if (baseline.count) j["baseline"] = baseline.toJson();
    if (level.count) j["level"] = level.toJson();
    if (difference.count) j["difference"] = difference.toJson();
    if (settled) j["settled"] = settled->toJson();
    if (settledDifference) j["settledDifference"] = settledDifference->toJson();
    if (open) j["open"] = open->toJson();
    return j;
}

} // inline namespace jf
