// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "machine/JPCell.h"
#include "tasks/JPBoardLocator.h"
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

    // A camera on the head: over the head's homing mark, then measured with
    // known moves. A fixed camera: a nozzle's tip held over it (asked first,
    // as a nozzle goes down to it) and moved about.
    void calibrate();
    // Look at the homing mark and say how far it is from its setting.
    void visualTest();
    // Finish a home with the camera (JPVisualHoming), through the first
    // calibrated camera on a head that homes visually, shown while it works.
    // Says why not when there is no such camera. Nothing when no head homes
    // visually.
    void visualHome();

    // The camera shown's mount; null when there is none.
    const JPMountConfig* shownMount() const {
        return m_cameras.shownFeed() ? &m_cameras.shownFeed()->config().mount : nullptr;
    }
    // Where the camera shown is looking, when it rides on a head; else why not.
    bool shownCameraView(double& x, double& y, std::string& why) const;
    // The camera shown's calibration and where it is looking, when it is a
    // calibrated camera on a head (for drawing the machine over its picture).
    bool shownCameraLook(JPCameraCalibration& calibration, double& viewX, double& viewY) const;
    // Move the camera shown to look at (x, y); false (and the status bar says
    // why) when it cannot.
    bool lookAt(double x, double y);
    // Find `board` by its fiducials from `guess` with the camera shown; the
    // result comes to `done` on the main thread.
    void locateBoard(const JPBoard& board, const JPBoardSide& guess,
                     std::function<void(const JPBoardLocator::Result&)> done);
    // A task is under way.
    bool busy() const { return m_busy; }

private:
    // Runs `task` on the worker; its answer (ok, words) comes back on the main
    // thread to `done`. `progress` from the task is shown as it goes.
    using Task = std::function<bool(std::string& words, const std::function<void(const std::string&)>& progress)>;
    void run(const std::string& name, Task task, std::function<void(bool)> done = nullptr);
    void calibrateFixed();
    // A new calibration in use, and saved in the cell file.
    void keepCalibration(const std::string& cameraId, const JPCameraCalibration& calibration);
    // What stops a task starting, in words; empty when it can.
    std::string notReady(bool needsCalibration, bool needsHomingMark) const;
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
