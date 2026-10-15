// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include <j/config/Json.h>

#include <optional>
#include <string>
#include <vector>

inline namespace jf {

// A nozzle tip's part detection measured, so its vacuum ranges come from the
// machine's own readings, not guesses. Part off: the part-off check's pulse
// made kPulses times with nothing on the nozzle, up in the air: the level
// before each pulse (`baseline`), as the valve closes (`level`, and its
// `difference` from the baseline) and, with a Valve close wait, after it
// (`settled`, `settledDifference`). Part on: the vacuum switched on as a pick
// does it, the pick's dwell, then the level read kSamples times over kHoldMs,
// once with nothing on the nozzle (`open`) and once with a part held
// (`baseline` the reading as the dwell ends, `level` the rest, `difference`
// each from the baseline).
//
// A range is told apart from the other state's level: a part held from none
// (part on), a nozzle blocked (as by a part held) from an open one (part off).
// Its edge towards the other is halfway between the two (their deviations
// over a few seconds say nothing of how far a level moves over the pump's
// cycle, which is far more); its far edge as far again as the level
// itself (to 0, or twice it), so a level drifting (the pump's does, by
// thousands over minutes) stays in it as long as it stays on its side. Two fewer than kSigmas
// deviations apart in all are too close to tell apart: no range. Without the
// other state measured, kSigmas deviations either side (at least the
// readings' spread and resolution), and said.
//
// A sensor on the pump's side of the valve reads the pump again as the valve
// closes, whatever is on the nozzle: when the level comes most of the way
// back within the Valve close wait, the wait goes (0), and the check reads
// the level as the valve closes.
struct JPVacuumMeasure {
    // What a set of readings came to.
    struct Stats {
        int    count = 0;
        double mean = 0, sd = 0, lowest = 0, highest = 0;
        // The smallest step between two readings (1 for whole numbers that
        // were all the same).
        double resolution = 0;

        static Stats of(const std::vector<double>& values);
        // The deviation reckoned with: never under half the resolution.
        double sigma() const;
        // How far either side of the mean a range reaches without another
        // level to tell it from.
        double halfWidth() const;
        static Stats fromJson(const JJson& j);
        JJson toJson() const;
    };
    struct Range {
        double low = 0, high = 0;
        bool   bounded = false;   // its edge set by the other state's level
    };
    // The ranges for a tip's part on or part off sensing, the method they are
    // for (Absolute, when it had none) and, part off, the Valve close wait
    // when it is to change; none where they cannot be told apart.
    struct Ranges {
        std::string          method;
        std::optional<Range> vacuum, difference;
        std::optional<int>   dwellMs;
        std::vector<std::string> notes;
        bool                 tooClose = false;   // measured, but not told apart by the method
    };
    // The readings a measuring made (JPCell::measureVacuumAndWait): before
    // each pulse (part on: as the dwell ends), as the valve closes (part on:
    // the levels held), after the Valve close wait (part off with one).
    struct Readings {
        std::vector<double> before, during, after;
        std::vector<double> pump;   // part on: the level before the valve opened
    };

    std::string          when;
    Stats                baseline, level, difference;
    std::optional<Stats> settled, settledDifference;
    std::optional<Stats> open;
    // Each level as a share of the pump's just before (part off: before the
    // pulse; part on: before the valve opened): with a part held, and (part
    // on) without one. Under 1 when the sensor reads the pump's side of the
    // valve, where the Ratio method takes the pump's swings out.
    std::optional<Stats> ratio, openRatio;

    // How it is measured.
    static constexpr int    kPulses = 20;
    static constexpr int    kRestMs = 500;     // between pulses
    static constexpr int    kSamples = 20;
    static constexpr int    kHoldMs = 3000;
    static constexpr double kSigmas = 6;
    // Back this share of the way to the baseline within the Valve close wait:
    // the sensor reads the pump's side of the valve.
    static constexpr double kRecovered = 0.5;

    bool held() const { return level.count > 0; }
    // The sensor reads the pump's side of the valve: the shares measured are under 1.
    bool pumpSide() const;

    // A part off measuring of `r`, or a part on one with (`withPart`) or
    // without a part, taken into this one (the other half of a part on
    // measuring kept).
    void take(const Readings& r, bool partOn, bool withPart);
    // The range for `s`, told from `other` (see above); none when too close.
    static std::optional<Range> around(const Stats& s, const std::optional<Stats>& other);
    // How far apart two levels are, in their summed deviations.
    static double separation(const Stats& a, const Stats& b);
    // The ranges for part on (`partOn`) or part off, with `method` as the tip
    // has it ("None", "Absolute", "Difference" or "Ratio") and, part off, its Valve
    // close wait and the tip's part on measuring (`partner`: its level with
    // a part is a blocked nozzle's, its level without one the pump pulled
    // down by an open nozzle). Read on the pump's side of the valve, Ratio is
    // set (the pump's swings taken out) when it tells the two apart; a method
    // that cannot tell them apart where another can is changed to it; said.
    Ranges ranges(bool partOn, const std::string& method, int dwellMs, const JPVacuumMeasure* partner) const;

    static JPVacuumMeasure fromJson(const JJson& j);
private:
    Ranges rangesBy(bool partOn, const std::string& method, int dwellMs, const JPVacuumMeasure* partner) const;
    Ranges rangesByRatio(bool partOn, int dwellMs, const JPVacuumMeasure* partner) const;
public:
    JJson toJson() const;
};

} // inline namespace jf
