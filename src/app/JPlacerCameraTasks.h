// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "machine/JPCell.h"
#include "ui/JPCameraPanel.h"

#include <j/app/JAppWindow.h>

#include <functional>
#include <memory>
#include <string>
#include <thread>

inline namespace jf {

// The camera panel's Calibrate and Visual Test, on the camera shown. Each runs
// on a thread of its own (it waits on moves and pictures) while the window
// carries on; what it is doing and how it went show in the panel's note and
// the status bar. A calibration is kept in the cell and saved to its file.
class JPlacerCameraTasks {
public:
    JPlacerCameraTasks(JAppWindow& window, JPCell& cell, JPCameraPanel& cameras, std::string cellPath);
    // Waits for a task under way: the cell and camera it drives go next.
    ~JPlacerCameraTasks();

    JPlacerCameraTasks(const JPlacerCameraTasks&)            = delete;
    JPlacerCameraTasks& operator=(const JPlacerCameraTasks&) = delete;

    // Move the camera over the head's homing mark, then measure it with known moves.
    void calibrate();
    // Look at the homing mark and say how far it is from its setting.
    void visualTest();
    // Finish a home with the camera (JPVisualHoming), through the first
    // calibrated camera on a head that homes visually, shown while it works.
    // Says why not when there is no such camera. Nothing when no head homes
    // visually.
    void visualHome();

private:
    // Runs `task` on the worker; its answer (ok, words) comes back on the main
    // thread to `done`. `progress` from the task is shown as it goes.
    using Task = std::function<bool(std::string& words, const std::function<void(const std::string&)>& progress)>;
    void run(const std::string& name, Task task, std::function<void(bool)> done = nullptr);
    // What stops a task starting, in words; empty when it can.
    std::string notReady(bool needsCalibration) const;
    const JPHeadConfig* head(const JPCameraConfig& camera) const;

    JAppWindow&           m_window;
    JPCell&               m_cell;
    JPCameraPanel&        m_cameras;
    std::string           m_cellPath;
    std::thread           m_worker;
    bool                  m_busy = false;   // main thread's
    std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);
};

} // inline namespace jf
