// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPCaptureSource.h"
#include "JPFrame.h"

#include <chrono>
#include <optional>
#include <string>

inline namespace jf {

// A capture device's exposure set, by hand, for a picture of `target`
// brightness (0..255) under the light it has now: for each picture taken for
// vision, where the light is not always the same. A picture looked at, the
// exposure scaled by how far its brightness is from the target (a picture
// grows about as bright as its exposure is long), and looked at again, until
// it is near enough or the tries are used up (each look logged, at Debug). Each look waits out the
// pictures the device gives before it shows a new setting (JPAutoTune's).
// Driven from the thread that captures (JPCameraFeed): each picture shown to
// see(), and step() between.
class JPOneShotExposure {
public:
    // Near enough: within this many levels (of 255) of the target; at most so many exposures tried (the first
    // the one it has); a picture this bright or more is too bright to scale from (its exposure divided by
    // kSaturatedStep instead).
    static constexpr double kNearLevels = 8, kSaturated = 250, kSaturatedStep = 4;
    // Six: the bench's bottom camera, opened after a restart at its stored exposure (three times the one it
    // wanted), ended four tries 9 levels out (137 at 448; 129 at 418 after): a fifth would have had it.
    static constexpr int    kTries = 6;

    struct Result {
        bool        ok = false;
        double      exposure = 0, brightness = 0;
        int         pictures = 0;   // all it looked at or passed over
        std::string why;
    };

    // Begun: false (`why`) when the device has no exposure it can be set to by hand.
    bool start(JPCaptureSource& source, double target, std::string& why);
    // A picture the device gave (as taken).
    void see(const JPFrame& frame);
    // On with it; once done, how it went.
    std::optional<Result> step(JPCaptureSource& source);

private:
    void set(JPCaptureSource& source, double exposure);

    double m_target = 0, m_lo = 0, m_hi = 0;
    double m_exposure = 0;
    int    m_tries = 0, m_seen = 0, m_pictures = 0;
    double m_sum = 0;
    bool   m_running = false;
};

} // inline namespace jf
