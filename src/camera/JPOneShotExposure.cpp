// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPOneShotExposure.h"

#include "JPAutoTune.h"

#include "common/JPlacerLog.h"

#include <j/core/Log.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

inline namespace jf {

namespace {

constexpr const char* kExposure = "exposure";

} // namespace

bool JPOneShotExposure::start(JPCaptureSource& source, double target, std::string& why) {
    const JJson have = source.controls();
    const JJson& e = have[kExposure];
    if (!e["value"].isNumber() || !e["min"].isNumber() || !e["max"].isNumber() || e["max"].number() <= e["min"].number()) {
        why = source.describe() + " has no exposure to set";
        return false;
    }
    m_target = target;
    m_lo = e["min"].number();
    m_hi = e["max"].number();
    m_tries = 1;
    m_pictures = 0;
    m_running = true;
    // Looked at as it is first; set by hand (held where it is) when it was automatic.
    if (e["auto"].boolean()) set(source, e["value"].number());
    else {
        m_exposure = e["value"].number();
        m_seen = 0;
        m_sum = 0;
    }
    return true;
}

void JPOneShotExposure::set(JPCaptureSource& source, double exposure) {
    m_exposure = std::clamp(std::round(exposure), m_lo, m_hi);
    JJson want = JJson::object();
    want[kExposure]["auto"] = false;
    want[kExposure]["value"] = m_exposure;
    source.setControls(want);
    m_seen = 0;
    m_sum = 0;
}

void JPOneShotExposure::see(const JPFrame& frame) {
    if (!m_running) return;
    ++m_pictures;
    if (++m_seen <= JPAutoTune::kSettleFrames) return;
    m_sum += JPAutoTune::brightnessOf(frame);
}

std::optional<JPOneShotExposure::Result> JPOneShotExposure::step(JPCaptureSource& source) {
    if (!m_running || m_seen < JPAutoTune::kSettleFrames + JPAutoTune::kLookFrames) return std::nullopt;
    const double b = m_sum / double(m_seen - JPAutoTune::kSettleFrames);
    Result r;
    r.exposure = m_exposure;
    r.brightness = b;
    r.pictures = m_pictures;
    const bool near = std::abs(b - m_target) <= kNearLevels;
    const double next = b >= kSaturated ? m_exposure / kSaturatedStep : m_exposure * m_target / std::max(b, 1.0);
    const bool stuck = std::clamp(std::round(next), m_lo, m_hi) == m_exposure;   // at its least or most
    JLOGC(JPlacerLog::kCamera, JLogLevel::Debug) << source.describe() << ": exposure " << m_exposure << " gave brightness " << b
                                                  << " (try " << m_tries << " of " << kTries << ")";
    if (near || stuck || m_tries >= kTries) {
        m_running = false;
        r.ok = near;
        if (!near) {
            char buf[160];
            std::snprintf(buf, sizeof buf, "brightness %.0f, not the %.0f wanted, at exposure %.0f%s", b, m_target, m_exposure,
                          stuck ? " (as far as it goes)" : "");
            r.why = buf;
        }
        return r;
    }
    ++m_tries;
    set(source, next);
    return std::nullopt;
}

} // inline namespace jf
