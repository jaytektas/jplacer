// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPCaptureSource.h"
#include "JPFrame.h"

#include <j/config/Json.h>

#include <chrono>
#include <optional>
#include <string>
#include <vector>

inline namespace jf {

// Defaults, then Auto-Tune, on a capture device's own settings: each to the
// device's default, those with an automatic mode left to it at least a
// moment (`autoMs`) and until its picture holds steady (a camera's automatic
// exposure can take seconds to come back from far off), the picture it gives
// then looked at; then all switched to
// manual. Many cameras do not say what their automatic exposure and white
// balance settled on (they read back the last value set by hand), so those
// two are found from the picture instead: set together, a step at a time, a
// few pictures each, closing in on the values that give the picture the
// camera gave by itself (its brightness, and its warmth: red against blue).
// Then held a moment (`holdMs`) and read back, kept by hand. Driven from the
// thread that captures (JPCameraFeed): each picture shown to see(), and
// step() between.
class JPAutoTune {
public:
    using Clock = std::chrono::steady_clock;

    // Pictures a step lets pass before it looks, then how many it looks at; steps in which each search halves.
    // Passed: those the driver may have queued (3) and those a camera takes to show a new setting (a UVC camera,
    // 2, measured).
    static constexpr int kSettleFrames = 5, kLookFrames = 1, kHalvings = 6;
    // Tuned only when the picture with the values found is this near the aim: within kReachLevels (of 255) or
    // kReachShare of the aim's brightness, whichever is more, and kReachWarmth.
    static constexpr double kReachLevels = 12, kReachShare = 0.15, kReachWarmth = 0.05;
    // Steady: over the last kSteadyMs, the pictures' brightness within kSteadyLevels (of 255) and warmth within
    // kSteadyWarmth; those pictures are the aim. Waited for at most kAutoMostMs, the last of them then the aim.
    static constexpr int    kSteadyMs = 400, kAutoMostMs = 6000;
    static constexpr double kSteadyLevels = 2.0, kSteadyWarmth = 0.01;

    JPAutoTune(int autoMs, int holdMs) : m_autoMs(autoMs), m_holdMs(holdMs) {}

    // Begun: false when the device has no settings of its own (nothing to tune).
    bool start(JPCaptureSource& source, Clock::time_point now);
    // A picture the device gave (as taken), at `when`.
    void see(const JPFrame& frame, Clock::time_point when);
    // On with it; the settings arrived at once done (each "auto" false, "value" held), else none yet. Done,
    // reached() says whether they gave the picture aimed for (got(): the one they gave).
    std::optional<JJson> step(JPCaptureSource& source, Clock::time_point now);

    // A picture's brightness (0..255) and warmth (its red less its blue, against all three).
    struct Look {
        double brightness = 0, warmth = 0;
    };
    static Look lookAt(const JPFrame& frame);
    // The picture aimed for (the device's own, steady), once the automatic moment is over.
    Look aim() const { return m_aim; }
    Look got() const { return m_got; }
    bool reached() const;

private:
    enum class Step { Auto, Search, Check, Hold, Done };
    // One setting found from the picture: its range, the value tried, and which way the picture goes.
    struct Search {
        std::string name;
        bool        brightness;   // else warmth
        bool        geometric;    // halved on a log scale (exposure)
        double      lo, hi, tried = 0;
        double      atLo = 0, atHi = 0;
    };
    enum class Phase { Low, High, Halving };
    void tryValues(JPCaptureSource& source, Clock::time_point now);

    int                 m_autoMs, m_holdMs;
    Step                m_step = Step::Done;
    Clock::time_point   m_started {}, m_due {};
    JJson               m_tuning;   // what is set: each setting's "auto" and "value"
    std::vector<std::pair<Clock::time_point, Look>> m_autoLooks;
    Look                m_aim, m_got;
    std::vector<Search> m_searches;
    Phase               m_phase = Phase::Low;
    int                 m_halvings = 0;
    // The pictures since the values were last set: passed over, then looked at.
    int                 m_seen = 0;
    Look                m_sum;
};

} // inline namespace jf
