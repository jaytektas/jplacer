// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPAutoTune.h"

#include "JPVisionDeviceSettings.h"

#include <algorithm>
#include <cmath>

inline namespace jf {

namespace {

// Every so many pixels looked at: enough for a picture's overall look.
constexpr size_t kStride = 61;

// The setting found from the picture (as a device names it): exposure, on a log scale.
constexpr const char* kExposure = "exposure";

} // namespace

double JPAutoTune::brightnessOf(const JPFrame& frame) {
    double sum = 0;
    size_t n = 0;
    for (size_t i = 0; i + 2 < frame.rgba.size(); i += kStride * 4, ++n) sum += frame.rgba[i] + frame.rgba[i + 1] + frame.rgba[i + 2];
    return n ? sum / (3.0 * double(n)) : 0;
}

bool JPAutoTune::reached() const {
    return std::abs(m_got - m_aim) <= std::max(kReachLevels, kReachShare * m_aim);
}

bool JPAutoTune::start(JPCaptureSource& source, Clock::time_point now) {
    // Those vision wants so (JPVisionDeviceSettings) by hand, as it wants them; any other to the device's
    // default, or left to it where it can be.
    m_tuning = JJson::object();
    m_search.reset();
    m_autoLooks.clear();
    const JJson have = source.controls();   // kept: its settings are walked
    for (const auto& [name, c] : have.obj()) {
        JJson want = JJson::object();
        if (const auto value = JPVisionDeviceSettings::wanted(name, c)) {
            want["auto"] = false;
            want["value"] = *value;
        } else {
            want["auto"] = c["auto"].isBool();
            if (!c["auto"].isBool()) want["value"] = c["default"].number(c["value"].number());
        }
        m_tuning[name] = want;
        // Exposure found from the picture, where the device says its range.
        if (name == kExposure && c["auto"].isBool() && c["min"].isNumber() && c["max"].isNumber()
            && c["max"].number() > c["min"].number())
            m_search = Search { c["min"].number() > 0, c["min"].number(), c["max"].number() };
    }
    if (m_tuning.obj().empty()) return false;
    source.setControls(m_tuning);
    m_step = Step::Auto;
    m_started = now;
    m_due = now + std::chrono::milliseconds(m_autoMs);
    return true;
}

void JPAutoTune::see(const JPFrame& frame, Clock::time_point when) {
    if (m_step == Step::Auto) {
        m_autoLooks.push_back({ when, brightnessOf(frame) });
        return;
    }
    if (m_step != Step::Search && m_step != Step::Check) return;
    // Passed over while the device takes the value, then looked at.
    if (++m_seen <= kSettleFrames) return;
    m_sum += brightnessOf(frame);
}

void JPAutoTune::tryValue(JPCaptureSource& source) {
    Search& s = *m_search;
    s.tried = m_phase == Phase::Low ? s.lo : m_phase == Phase::High ? s.hi : s.geometric ? std::sqrt(s.lo * s.hi) : (s.lo + s.hi) / 2;
    JJson want = JJson::object();
    want["auto"] = false;
    want["value"] = std::round(s.tried);
    m_tuning[kExposure] = want;
    source.setControls(m_tuning);
    m_seen = 0;
    m_sum = 0;
}

std::optional<JJson> JPAutoTune::step(JPCaptureSource& source, Clock::time_point now) {
    if (m_step == Step::Done || now < m_due) return std::nullopt;
    if (m_step == Step::Auto) {
        // The aim: the device's brightness once it holds steady (else, waited long enough, its last).
        const auto from = now - std::chrono::milliseconds(kSteadyMs);
        double sum = 0, least = 1e9, most = -1e9;
        int n = 0;
        for (const auto& [when, b] : m_autoLooks)
            if (when >= from) {
                sum += b;
                least = std::min(least, b);
                most = std::max(most, b);
                ++n;
            }
        const bool steady = n > 1 && most - least <= kSteadyLevels;
        if (!steady && now - m_started < std::chrono::milliseconds(kAutoMostMs)) return std::nullopt;
        if (n) m_aim = sum / n;
        m_got = m_aim;   // what it holds, where nothing is searched for
        // Switched to manual: the device holds what it settled on (where it says so; else found below).
        for (auto& [name, want] : m_tuning.obj()) {
            if (!want["auto"].boolean()) continue;
            want = JJson::object();
            want["auto"] = false;
        }
        source.setControls(m_tuning);
        if (n && m_search) {
            m_step = Step::Search;
            m_phase = Phase::Low;
            m_halvings = 0;
            tryValue(source);
            return std::nullopt;
        }
        m_step = Step::Hold;
        m_due = now + std::chrono::milliseconds(m_holdMs);
        return std::nullopt;
    }
    if (m_step == Step::Search) {
        if (m_seen < kSettleFrames + kLookFrames) return std::nullopt;   // not looked yet
        Search& s = *m_search;
        const double b = looked();
        if (m_phase == Phase::Low) s.atLo = b;
        else if (m_phase == Phase::High) s.atHi = b;
        else if ((b < m_aim) == (s.atHi >= s.atLo)) s.lo = s.tried;   // the aim in the upper half (brighter as it rises)
        else s.hi = s.tried;
        if (m_phase == Phase::Low) m_phase = Phase::High;
        else if (m_phase == Phase::High) m_phase = Phase::Halving;
        else ++m_halvings;
        if (m_halvings < kHalvings) {
            tryValue(source);
            return std::nullopt;
        }
        // Found: the middle of what is left, then the picture it gives looked at.
        m_phase = Phase::Halving;
        tryValue(source);
        m_step = Step::Check;
        return std::nullopt;
    }
    if (m_step == Step::Check) {
        if (m_seen < kSettleFrames + kLookFrames) return std::nullopt;
        m_got = looked();
        m_step = Step::Hold;
        m_due = now + std::chrono::milliseconds(m_holdMs);
        return std::nullopt;
    }
    // What it holds, kept by hand from now on.
    JJson tuned = JJson::object();
    const JJson held = source.controls();
    for (const auto& [name, c] : held.obj()) {
        tuned[name]["auto"] = false;
        tuned[name]["value"] = c["value"].number();
    }
    source.setControls(tuned);
    m_step = Step::Done;
    return tuned;
}

} // inline namespace jf
