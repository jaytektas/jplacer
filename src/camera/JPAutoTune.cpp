// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPAutoTune.h"

#include <algorithm>
#include <cmath>

inline namespace jf {

namespace {

// Every so many pixels looked at: enough for a picture's overall look.
constexpr size_t kStride = 61;

// The settings found from the picture (what each is, as a device names it).
struct Found {
    const char* name;
    bool        brightness, geometric;
};
constexpr Found kFound[] = { { "exposure", true, true }, { "white-balance", false, false } };

} // namespace

JPAutoTune::Look JPAutoTune::lookAt(const JPFrame& frame) {
    double r = 0, g = 0, b = 0;
    size_t n = 0;
    for (size_t i = 0; i + 2 < frame.rgba.size(); i += kStride * 4, ++n) {
        r += frame.rgba[i];
        g += frame.rgba[i + 1];
        b += frame.rgba[i + 2];
    }
    Look l;
    if (!n) return l;
    l.brightness = (r + g + b) / (3.0 * double(n));
    l.warmth = (r + g + b) > 0 ? (r - b) / (r + g + b) : 0;
    return l;
}

bool JPAutoTune::reached() const {
    return std::abs(m_got.brightness - m_aim.brightness) <= std::max(kReachLevels, kReachShare * m_aim.brightness)
        && std::abs(m_got.warmth - m_aim.warmth) <= kReachWarmth;
}

bool JPAutoTune::start(JPCaptureSource& source, Clock::time_point now) {
    // Each setting to the device's default; those it can, left to it.
    m_tuning = JJson::object();
    m_searches.clear();
    m_autoLooks.clear();
    const JJson have = source.controls();   // kept: its settings are walked
    for (const auto& [name, c] : have.obj()) {
        JJson want = JJson::object();
        want["auto"] = c["auto"].isBool();
        if (!c["auto"].isBool()) want["value"] = c["default"].number(c["value"].number());
        m_tuning[name] = want;
        // Exposure and white balance found from the picture, where the device says their range.
        for (const Found& f : kFound)
            if (name == f.name && c["auto"].isBool() && c["min"].isNumber() && c["max"].isNumber()
                && c["max"].number() > c["min"].number()) {
                Search s;
                s.name = name;
                s.brightness = f.brightness;
                s.geometric = f.geometric && c["min"].number() > 0;
                s.lo = c["min"].number();
                s.hi = c["max"].number();
                m_searches.push_back(s);
            }
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
        m_autoLooks.push_back({ when, lookAt(frame) });
        return;
    }
    if (m_step != Step::Search && m_step != Step::Check) return;
    // Passed over while the device takes the values, then looked at.
    if (++m_seen <= kSettleFrames) return;
    const Look l = lookAt(frame);
    m_sum.brightness += l.brightness;
    m_sum.warmth += l.warmth;
}

void JPAutoTune::tryValues(JPCaptureSource& source, Clock::time_point now) {
    for (Search& s : m_searches) {
        s.tried = m_phase == Phase::Low ? s.lo : m_phase == Phase::High ? s.hi
                  : s.geometric ? std::sqrt(s.lo * s.hi) : (s.lo + s.hi) / 2;
        JJson want = JJson::object();
        want["auto"] = false;
        want["value"] = std::round(s.tried);
        m_tuning[s.name] = want;
    }
    source.setControls(m_tuning);
    m_seen = 0;
    m_sum = Look {};
    m_due = now;
}

std::optional<JJson> JPAutoTune::step(JPCaptureSource& source, Clock::time_point now) {
    if (m_step == Step::Done || now < m_due) return std::nullopt;
    if (m_step == Step::Auto) {
        // The aim: the device's picture once it holds steady (else, waited long enough, its last).
        const auto from = now - std::chrono::milliseconds(kSteadyMs);
        Look sum, least { 1e9, 1e9 }, most { -1e9, -1e9 };
        int n = 0;
        for (const auto& [when, l] : m_autoLooks)
            if (when >= from) {
                sum.brightness += l.brightness;
                sum.warmth += l.warmth;
                least = { std::min(least.brightness, l.brightness), std::min(least.warmth, l.warmth) };
                most = { std::max(most.brightness, l.brightness), std::max(most.warmth, l.warmth) };
                ++n;
            }
        const bool steady = n > 1 && most.brightness - least.brightness <= kSteadyLevels && most.warmth - least.warmth <= kSteadyWarmth;
        if (!steady && now - m_started < std::chrono::milliseconds(kAutoMostMs)) return std::nullopt;
        if (n) m_aim = { sum.brightness / n, sum.warmth / n };
        // Switched to manual: the device holds what it settled on (where it says so; else found below).
        for (auto& [name, want] : m_tuning.obj()) {
            if (!want["auto"].boolean()) continue;
            want = JJson::object();
            want["auto"] = false;
        }
        source.setControls(m_tuning);
        m_got = m_aim;   // what it holds, where nothing is searched for
        if (n && !m_searches.empty()) {
            m_step = Step::Search;
            m_phase = Phase::Low;
            m_halvings = 0;
            tryValues(source, now);
            return std::nullopt;
        }
        m_step = Step::Hold;
        m_due = now + std::chrono::milliseconds(m_holdMs);
        return std::nullopt;
    }
    if (m_step == Step::Search) {
        if (m_seen < kSettleFrames + kLookFrames) return std::nullopt;   // not looked yet
        const double looks = double(m_seen - kSettleFrames);
        const Look got { m_sum.brightness / looks, m_sum.warmth / looks };
        for (Search& s : m_searches) {
            const double value = s.brightness ? got.brightness : got.warmth, aim = s.brightness ? m_aim.brightness : m_aim.warmth;
            if (m_phase == Phase::Low) s.atLo = value;
            else if (m_phase == Phase::High) s.atHi = value;
            else {
                // Which half the aim is in: the picture goes from atLo to atHi as the value goes from lo to hi.
                const bool rising = s.atHi >= s.atLo;
                if ((value < aim) == rising) s.lo = s.tried;
                else s.hi = s.tried;
            }
        }
        if (m_phase == Phase::Low) m_phase = Phase::High;
        else if (m_phase == Phase::High) m_phase = Phase::Halving;
        else ++m_halvings;
        if (m_halvings < kHalvings) {
            tryValues(source, now);
            return std::nullopt;
        }
        // Found: the middle of what is left, then the picture it gives looked at.
        for (Search& s : m_searches) {
            JJson want = JJson::object();
            want["auto"] = false;
            want["value"] = std::round(s.geometric ? std::sqrt(s.lo * s.hi) : (s.lo + s.hi) / 2);
            m_tuning[s.name] = want;
        }
        source.setControls(m_tuning);
        m_seen = 0;
        m_sum = Look {};
        m_step = Step::Check;
        return std::nullopt;
    }
    if (m_step == Step::Check) {
        if (m_seen < kSettleFrames + kLookFrames) return std::nullopt;
        const double looks = double(m_seen - kSettleFrames);
        m_got = { m_sum.brightness / looks, m_sum.warmth / looks };
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
