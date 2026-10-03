// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "machine/JPCell.h"
#include "tasks/JPBacklashCalibrator.h"
#include "tasks/JPBoardLocator.h"
#include "ui/JPCameraPanel.h"

#include <j/app/JAppWindow.h>

#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>

inline namespace jf {

// The camera panels' Calibrate and Visual Test, each on its own camera, and
// the camera work of homing and the board, through the camera on the head.
// Each runs on a thread of its own (it waits on moves and pictures) while the
// window carries on; what it is doing and how it went show in its panel's note
// and the status bar. The panel a task uses is brought to the front, and kept
// running while the task lasts. A calibration is kept in the cell and saved to
// its file.
class JPlacerCameraTasks {
public:
    // `cameras`: a panel for each of the cell's cameras. `bringForward`: show
    // a panel (the front tab of its dock group).
    JPlacerCameraTasks(JAppWindow& window, JPCell& cell, std::vector<JPCameraPanel*> cameras,
                       std::function<void(JPCameraPanel&)> bringForward, std::string cellPath);
    // Waits for a task under way: the cell and camera it drives go next.
    ~JPlacerCameraTasks();

    JPlacerCameraTasks(const JPlacerCameraTasks&)            = delete;
    JPlacerCameraTasks& operator=(const JPlacerCameraTasks&) = delete;

    // A calibration was made, kept in the cell and saved (for what shows it).
    std::function<void(const std::string& cameraId, const JPCameraCalibration&)> onCalibrated;

    // A camera on the head: over the head's homing mark, then measured with
    // known moves. A fixed camera: a nozzle's tip held over it (asked first,
    // as a nozzle goes down to it) and moved about.
    void calibrate(JPCameraPanel& camera);
    // Look at the homing mark and say how far it is from its setting.
    void visualTest(JPCameraPanel& camera);
    // The settling test: a camera on a head moved (dx, dy) and back, then
    // let settle, how it settled kept (JPSettleTrace); a fixed camera only
    // let settle. `done` (main thread): the trace.
    void settleTest(JPCameraPanel& camera, double dx, double dy, std::function<void(const JPSettleTrace&)> done);
    // Measure an X or Y axis's backlash with the head camera over the head's
    // homing mark (JPBacklashCalibrator). `done` (main thread): what it found,
    // in use already, for the owner to keep.
    void calibrateBacklash(const std::string& axisId, std::function<void(const JPBacklashCalibrator::Result&)> done);
    // Finish a home with the camera (JPVisualHoming), through the first
    // calibrated camera on a head that homes visually. Says why not when
    // there is no such camera. Nothing when no head homes visually. `done`
    // (on the main thread): homing is finished, true when it went well (or
    // there was nothing more to do).
    void visualHome(std::function<void(bool)> done = nullptr);

    // The camera the board is worked with: the first one riding on a head,
    // a calibrated one first. Null when no camera rides on a head.
    JPCameraPanel* headCamera() const;
    // Where the head camera is looking; else why not.
    bool headCameraView(double& x, double& y, std::string& why) const;
    // A camera's calibration and where it is looking, when it is a calibrated
    // camera on a head (for drawing the machine over its picture).
    bool cameraLook(const std::string& cameraId, JPCameraCalibration& calibration, double& viewX, double& viewY) const;
    // Move the head camera to look at (x, y), and show it; false (and the
    // status bar says why) when it cannot.
    bool lookAt(double x, double y);
    // Find `board` by its fiducials from `guess` with the head camera; the
    // result comes to `done` on the main thread.
    void locateBoard(const JPBoard& board, const JPBoardSide& guess,
                     std::function<void(const JPBoardLocator::Result&)> done);
    // A task is under way.
    bool busy() const { return m_busy; }

private:
    // Runs `task` on the worker with `camera` shown and running; its answer
    // (ok, words) comes back on the main thread to `done`. `progress` from the
    // task is shown as it goes.
    using Task = std::function<bool(std::string& words, const std::function<void(const std::string&)>& progress)>;
    void run(JPCameraPanel& camera, const std::string& name, Task task, std::function<void(bool)> done = nullptr);
    void calibrateFixed(JPCameraPanel& camera);
    bool cameraView(const JPCameraConfig& camera, double& x, double& y, std::string& why) const;
    bool lookAt(JPCameraPanel& camera, double x, double y);
    // A new calibration in use, and saved in the cell file.
    // The scale at a second height into the first calibration.
    static void secondHeight(JPCameraCalibration& first, const JPCameraCalibration& second);
    // What a calibration found, in words.
    static std::string calibrated(const JPCameraConfig& cam, const JPCameraCalibration& c);
    void keepCalibration(const std::string& cameraId, const JPCameraCalibration& calibration);
    // What stops a task starting on `camera`, in words; empty when it can.
    std::string notReady(const JPCameraPanel* camera, bool needsCalibration, bool needsHomingMark) const;
    const JPHeadConfig* head(const JPCameraConfig& camera) const;

    JAppWindow&                         m_window;
    JPCell&                             m_cell;
    std::vector<JPCameraPanel*>         m_cameras;
    std::function<void(JPCameraPanel&)> m_bringForward;
    std::string                         m_cellPath;
    std::thread                         m_worker;
    bool                                m_busy = false;   // main thread's
    std::shared_ptr<bool>               m_alive = std::make_shared<bool>(true);
};

} // inline namespace jf
