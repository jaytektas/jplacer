// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "model/JPLocation.h"

#include <j/core/FrameTimer.h>

#include <functional>
#include <optional>
#include <string>

inline namespace jf {

// The Boards' Board Nudge: the board's X and Y moved by what the camera is jogged while it is on, so the part
// under the camera can be lined up by eye and every placement follows (a fiducial check a few hundredths out, all
// the parts out alike). Begun where the camera is; what it has been jogged since said as it changes; Apply moves the
// board by it, Cancel leaves it as it was. The next fiducial check sets the board's location again, as ever.
class JPBoardNudge {
public:
    struct Hooks {
        std::function<std::optional<JPLocation>()>       camera;   // where the head camera is; none: no machine
        std::function<void(const std::string& text)>     show;     // the instructions, shown again as the nudge changes
        std::function<void()>                            hide;
        std::function<void(double dxMm, double dyMm)>    apply;    // the board moved by this, along the machine's X and Y
        std::function<void(bool on)>                     lit;      // the button lit while it is on
    };

    explicit JPBoardNudge(Hooks hooks);

    bool active() const { return m_from.has_value(); }
    // Begun from where the camera is now; false (and why) with no machine.
    bool start(std::string& why);
    // The board moved by what was jogged, and done.
    void apply();
    // Done, the board as it was.
    void cancel();
    // The jog so far, in millimetres.
    double dx() const { return m_dx; }
    double dy() const { return m_dy; }

    // What it says while on, the jog so far given.
    static std::string text(double dxMm, double dyMm);

private:
    void follow();
    void stop();

    Hooks                      m_hooks;
    std::optional<JPLocation>  m_from;
    double                     m_dx = 0, m_dy = 0;
    std::string                m_shown;
    JFrameTimer                m_timer { 0.f };   // every frame
};

} // inline namespace jf
