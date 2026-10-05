// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "machine/JPCell.h"

#include <j/app/JAppWindow.h>

#include <atomic>
#include <functional>
#include <memory>
#include <thread>

inline namespace jf {

// The motion planner's Test Motion (Machine Setup › Machine, Motion Planner
// Diagnostics' Test): the tool chosen on the Jog panel run through the
// Motion Planner tab's places on a thread of its own (JPCell::testMotionAndWait),
// back from the last when the tool is nearer that one than the first, as
// OpenPnP's. What it found goes to `done`; a failure to the status bar.
class JPlacerTestMotion {
public:
    JPlacerTestMotion(JAppWindow& window, JPCell& cell);
    // Waits for a run under way.
    ~JPlacerTestMotion();

    JPlacerTestMotion(const JPlacerTestMotion&)            = delete;
    JPlacerTestMotion& operator=(const JPlacerTestMotion&) = delete;

    // Refused (the status bar says why) while one runs, or when the machine
    // is not ready. `done` (main thread): what the run found.
    void run(const JPMountConfig& tool, std::function<void(const JPMotionTestResult&)> done);

private:
    JAppWindow&           m_window;
    JPCell&               m_cell;
    std::thread           m_worker;
    std::atomic<bool>     m_busy { false };
    std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);
};

} // inline namespace jf
