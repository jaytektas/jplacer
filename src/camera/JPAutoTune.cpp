// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPAutoTune.h"

inline namespace jf {

bool JPAutoTune::start(JPCaptureSource& source, Clock::time_point now) {
    // Each setting to the device's default; those it can, left to it.
    m_tuning = JJson::object();
    const JJson have = source.controls();   // kept: its settings are walked
    for (const auto& [name, c] : have.obj()) {
        JJson want = JJson::object();
        want["auto"] = c["auto"].isBool();
        if (!c["auto"].isBool()) want["value"] = c["default"].number(c["value"].number());
        m_tuning[name] = want;
    }
    if (m_tuning.obj().empty()) return false;
    source.setControls(m_tuning);
    m_step = Step::Auto;
    m_due = now + std::chrono::milliseconds(m_autoMs);
    return true;
}

std::optional<JJson> JPAutoTune::step(JPCaptureSource& source, Clock::time_point now) {
    if (m_step == Step::Done || now < m_due) return std::nullopt;
    if (m_step == Step::Auto) {
        // Switched to manual: the device holds what it settled on.
        for (auto& [name, want] : m_tuning.obj()) {
            if (!want["auto"].boolean()) continue;
            want = JJson::object();
            want["auto"] = false;
        }
        source.setControls(m_tuning);
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
