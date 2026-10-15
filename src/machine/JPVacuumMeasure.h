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
// (the valve open for its time, closed for its dwell, the level read as the
// check reads it) made kPulses times with nothing on the nozzle, up in the
// air: the level before each pulse (`baseline`), after it (`level`), and
// their `difference`. Part on: the vacuum switched on as a pick does it, the
// pick's dwell, then the level read kSamples times over kHoldMs, once with
// nothing on the nozzle (`open`) and once with a part held (`baseline` the
// first reading, as a pick's dwell ends; `level` the rest; `difference`
// each from the baseline).
//
// A range is the readings' mean, kSigmas of their standard deviation either
// side (at least their spread, at least the readings' resolution). A part
// on range stops short of the level without a part: where the two are as
// many of their own deviations from each, the boundary. Fewer than kSigmas
// deviations apart in all, no part on range is set: one would pass a nozzle
// that has lost its part.
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
        // How far either side of the mean a range reaches.
        double halfWidth() const;
        static Stats fromJson(const JJson& j);
        JJson toJson() const;
    };
    struct Range {
        double low = 0, high = 0;
        bool   stopped = false;   // stopped short of the other level
    };
    // The ranges for a tip's part on or part off sensing, and the method
    // they are for (Absolute, when it had none); none for part on until it
    // is measured with a part.
    struct Ranges {
        std::string          method;
        std::optional<Range> vacuum, difference;
        // Worth knowing: the level with a part too close to the one
        // without, or the one without not measured.
        std::vector<std::string> notes;
    };

    std::string          when;
    Stats                baseline, level, difference;
    std::optional<Stats> open;

    // How it is measured.
    static constexpr int    kPulses = 20;
    static constexpr int    kRestMs = 500;     // between pulses
    static constexpr int    kSamples = 20;
    static constexpr int    kHoldMs = 3000;
    static constexpr double kSigmas = 6;

    bool held() const { return level.count > 0; }

    // The range about `s`, stopped short of `other` when it reaches past
    // their boundary.
    static Range around(const Stats& s, const std::optional<Stats>& other);
    // How far apart two levels are, in their summed deviations.
    static double separation(const Stats& a, const Stats& b);
    // The ranges for part on (`partOn`) or part off, with `method` as the
    // tip has it ("None", "Absolute" or "Difference").
    Ranges ranges(bool partOn, const std::string& method) const;

    static JPVacuumMeasure fromJson(const JJson& j);
    JJson toJson() const;
};

} // inline namespace jf
