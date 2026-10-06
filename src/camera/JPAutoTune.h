// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPCaptureSource.h"

#include <j/config/Json.h>

#include <chrono>
#include <optional>

inline namespace jf {

// Defaults, then Auto-Tune, on a capture device's own settings: each to the
// device's default, those with an automatic mode left to it a moment
// (`autoMs`), then switched to manual so the device holds what it settled
// on (`holdMs`), and those held values read back and kept by hand. Driven a
// step at a time from the thread that captures (JPCameraFeed).
class JPAutoTune {
public:
    using Clock = std::chrono::steady_clock;

    JPAutoTune(int autoMs, int holdMs) : m_autoMs(autoMs), m_holdMs(holdMs) {}

    // Begun: false when the device has no settings of its own (nothing to tune).
    bool start(JPCaptureSource& source, Clock::time_point now);
    // On with it; the settings arrived at once done (each "auto" false, "value" held), else none yet.
    std::optional<JJson> step(JPCaptureSource& source, Clock::time_point now);

private:
    enum class Step { Auto, Hold, Done };
    int               m_autoMs, m_holdMs;
    Step              m_step = Step::Done;
    Clock::time_point m_due {};
    JJson             m_tuning;
};

} // inline namespace jf
