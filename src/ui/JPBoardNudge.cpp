// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPBoardNudge.h"

#include <cstdio>

inline namespace jf {

JPBoardNudge::JPBoardNudge(Hooks hooks) : m_hooks(std::move(hooks)) {
    m_timer.timeout.connect([this] { follow(); });
}

std::string JPBoardNudge::text(double dxMm, double dyMm) {
    char b[256];
    std::snprintf(b, sizeof b,
                  "Jog the camera until it is on the part: the board moves by what you jog. Jogged so far: X %+.3f, "
                  "Y %+.3f mm.",
                  dxMm, dyMm);
    return b;
}

bool JPBoardNudge::start(std::string& why) {
    if (active()) return true;
    const std::optional<JPLocation> at = m_hooks.camera ? m_hooks.camera() : std::nullopt;
    if (!at) {
        why = "The machine is not connected.";
        return false;
    }
    m_from = at->convertToUnits(JPLengthUnit::Millimeters);
    m_dx = m_dy = 0;
    m_shown = text(0, 0);
    if (m_hooks.show) m_hooks.show(m_shown);
    if (m_hooks.lit) m_hooks.lit(true);
    m_timer.start();
    return true;
}

void JPBoardNudge::follow() {
    if (!active()) return;
    const std::optional<JPLocation> at = m_hooks.camera ? m_hooks.camera() : std::nullopt;
    if (!at) {   // the machine gone: nothing to nudge by
        cancel();
        return;
    }
    const JPLocation mm = at->convertToUnits(JPLengthUnit::Millimeters);
    m_dx = mm.x() - m_from->x();
    m_dy = mm.y() - m_from->y();
    const std::string said = text(m_dx, m_dy);
    if (said == m_shown) return;
    m_shown = said;
    if (m_hooks.show) m_hooks.show(m_shown);
}

void JPBoardNudge::apply() {
    if (!active()) return;
    follow();
    if (!active()) return;   // the machine went as it was read
    // Applied while it is still under way, then ended: ending it lets go of the board it is for (the Job panel's
    // hide hook), and applied after, it moved nothing.
    if (m_hooks.apply && (m_dx != 0 || m_dy != 0)) m_hooks.apply(m_dx, m_dy);
    stop();
}

void JPBoardNudge::cancel() {
    if (active()) stop();
}

void JPBoardNudge::stop() {
    m_timer.stop();
    m_from.reset();
    m_shown.clear();
    if (m_hooks.hide) m_hooks.hide();
    if (m_hooks.lit) m_hooks.lit(false);
}

} // inline namespace jf
