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
    // Each as a share of `base` (one value or one each); none without a level to take it from.
    auto share = [](const std::vector<double>& v, const std::vector<double>& base) -> std::optional<Stats> {
        std::vector<double> out;
        for (size_t i = 0; i < v.size() && !base.empty(); ++i) {
            const double b = base[std::min(i, base.size() - 1)];
            if (b == 0) return std::nullopt;   // (a sensor may read vacuum as negative: a share all the same)
            out.push_back(v[i] / b);
        }
        if (out.empty()) return std::nullopt;
        return Stats::of(out);
    };
    if (partOn && !withPart) {
        open = Stats::of(r.during);
        openRatio = share(r.during, r.pump);
        return;
    }
    ratio = share(r.during, partOn ? r.pump : r.before);
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
    // Halfway between the two: a few seconds' deviations say whether they can be told apart, not where the line goes,
    // for a level moves by far more over a pump's cycle (a reservoir switched between -75 and -95 kPa). Away from the
    // other, as far again as the level itself (to 0, or twice it), at least the boundary's distance: no risk that way.
    const double boundary = (s.mean + other->mean) / 2;
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

bool JPVacuumMeasure::pumpSide() const {
    auto under = [](const std::optional<Stats>& r) { return r && r->count > 0 && r->mean > 0 && r->mean < 1; };
    return under(ratio) || under(openRatio);
}

JPVacuumMeasure::Ranges JPVacuumMeasure::ranges(bool partOn, const std::string& method, int dwellMs,
                                                const JPVacuumMeasure* partner) const {
    const std::string asked = method == "None" || method.empty() ? (pumpSide() ? "Ratio" : "Absolute") : method;
    // On the pump's side of the valve, Ratio first: the pump's swings do not count.
    if (pumpSide() && asked != "Ratio") {
        Ranges byRatio = rangesBy(partOn, "Ratio", dwellMs, partner);
        if (byRatio.vacuum && !byRatio.tooClose) {
            byRatio.notes.insert(byRatio.notes.begin(),
                                 "The sensor reads the pump's side of the valve: Measurement Method set to Ratio, each level "
                                 "taken as a share of the pump's just before, so the pump's own swings do not count.");
            return byRatio;
        }
    }
    Ranges out = rangesBy(partOn, asked, dwellMs, partner);
    if (!out.tooClose) return out;
    // Another method, where it tells them apart.
    for (const char* other : { "Ratio", "Absolute", "Difference" }) {
        if (other == out.method || (std::string(other) == "Ratio" && !pumpSide())) continue;
        Ranges by = rangesBy(partOn, other, dwellMs, partner);
        if (by.tooClose || !by.vacuum) continue;
        std::vector<std::string> notes;
        for (const std::string& n : out.notes) notes.push_back(n == out.notes.back() ? n.substr(0, n.rfind(", and the ranges")) + "." : n);
        notes.push_back("Measured as " + by.method + ", they are told apart: Measurement Method set to " + by.method + ".");
        for (const std::string& n : by.notes)
            if (std::find(notes.begin(), notes.end(), n) == notes.end()) notes.push_back(n);
        by.notes = notes;
        return by;
    }
    return out;
}

JPVacuumMeasure::Ranges JPVacuumMeasure::rangesBy(bool partOn, const std::string& method, int dwellMs,
                                                  const JPVacuumMeasure* partner) const {
    Ranges out;
    out.method = method == "None" || method.empty() ? "Absolute" : method;
    const bool byDifference = out.method == "Difference";
    if (out.method == "Ratio") return rangesByRatio(partOn, dwellMs, partner);
    if (!partOn) {
        if (!held()) return out;
        if (dwellMs > 0 && !settled) {
            out.notes.push_back("Measured with no Valve close wait: Measure again for the wait now set.");
            return out;
        }
        bool afterClose = dwellMs > 0;
        if (afterClose) {
            // Back most of the way to the baseline as the valve closed: the sensor reads the pump, not the nozzle.
            // Only on the pump's side (the level before the pulse the higher): on the nozzle's, the line going back to
            // the air as the valve closes is what the wait is there to see.
            const double dip = baseline.mean - level.mean;
            const double back = dip <= 0 ? 0 : (settled->mean - level.mean) / dip;
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

JPVacuumMeasure::Ranges JPVacuumMeasure::rangesByRatio(bool partOn, int dwellMs, const JPVacuumMeasure* partner) const {
    Ranges out;
    out.method = "Ratio";
    if (!partOn) {
        if (!held()) return out;
        if (!ratio || !pumpSide()) {
            out.notes.push_back("The level before the pulse is not the pump's (the sensor reads the nozzle's side of the "
                                "valve): Ratio cannot be used.");
            out.tooClose = false;
            return out;
        }
        // Read as the valve closes: a Valve close wait would read the pump coming back, whatever is on the nozzle.
        if (dwellMs > 0) out.dwellMs = 0;
        std::optional<Stats> blocked = partner && partner->ratio ? partner->ratio : std::nullopt;
        if (!blocked)
            out.notes.push_back("Measure Part On With Part too: a held part's share is a blocked nozzle's, which Part Off's "
                                "range is told from. Until then it is only these readings' spread.");
        out.vacuum = around(*ratio, blocked);
        if (!out.vacuum) {
            out.tooClose = true;
            out.notes.push_back(tooClose("With nothing on the nozzle the pulse's share", *ratio, "with it blocked", *blocked));
        }
        return out;
    }
    if (!held() || !ratio) {
        out.notes.push_back("Measure With Part to set the ranges: so far only the level without a part is known.");
        return out;
    }
    out.vacuum = around(*ratio, openRatio);
    if (!openRatio) {
        out.notes.push_back("Not measured without a part: the range is not told from an empty nozzle's share, only these "
                            "readings' spread. Measure Without Part too.");
    } else if (!out.vacuum) {
        out.tooClose = true;
        out.notes.push_back(tooClose("With a part the share", *ratio, "without", *openRatio)
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
    m.ratio = statsIn(j["ratio"]);
    m.openRatio = statsIn(j["openRatio"]);
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
    if (ratio) j["ratio"] = ratio->toJson();
    if (openRatio) j["openRatio"] = openRatio->toJson();
    if (settled) j["settled"] = settled->toJson();
    if (settledDifference) j["settledDifference"] = settledDifference->toJson();
    if (open) j["open"] = open->toJson();
    return j;
}

} // inline namespace jf
