// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPSimulatedViewpoint.h"

#include <cmath>

inline namespace jf {

namespace {
// History kept beyond the lag and the shaking.
constexpr double kKeptS = 1.0;
// A change smaller than this is not a move (mm).
constexpr double kLeastMoveMm = 1e-9;
}

void JPSimulatedViewpoint::look(const JPSimulationConfig& sim, Clock::time_point now, double& x, double& y) {
    std::lock_guard lk(m_mutex);
    if (m_history.empty() || std::hypot(m_history.back().x - x, m_history.back().y - y) > kLeastMoveMm)
        m_history.push_back({ now, x, y });
    const double lag = sim.dynamic() ? std::max(0.0, sim.cameraLagS) : 0.0;
    const double shake = sim.dynamic() ? std::max(0.0, sim.vibrationDurationS) : 0.0;
    const auto keep = now - std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(lag + shake + kKeptS));
    while (m_history.size() > 1 && m_history[1].t < keep) m_history.pop_front();

    // Where it was the lag ago: the last place it had come to by then.
    const auto seen = now - std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(lag));
    size_t at = 0;
    for (size_t i = 0; i < m_history.size(); ++i)
        if (m_history[i].t <= seen) at = i;
    x = m_history[at].x;
    y = m_history[at].y;

    // Shaken after each move, along it, dying away to about 1% over the duration.
    if (sim.dynamic() && sim.vibrationAmplitudeMm != 0 && shake > 0)
        for (size_t i = 1; i <= at; ++i) {
            const double tau = std::chrono::duration<double>(seen - m_history[i].t).count();
            if (tau < 0 || tau > shake) continue;
            const double dx = m_history[i].x - m_history[i - 1].x, dy = m_history[i].y - m_history[i - 1].y;
            const double d = std::hypot(dx, dy);
            if (d < kLeastMoveMm) continue;
            const double a = sim.vibrationAmplitudeMm * std::exp(-tau * 4 / shake)
                           * std::cos(2 * M_PI * JPSimulationConfig::kVibrationHz * tau);
            x -= a * dx / d;
            y -= a * dy / d;
        }
    if (sim.imperfect()) {
        // Homed that far off (visual homing finds it), and X leaning with Y.
        x -= sim.homingErrorX;
        y -= sim.homingErrorY;
        x += sim.nonSquarenessFactor * y;
    }
}

} // inline namespace jf
