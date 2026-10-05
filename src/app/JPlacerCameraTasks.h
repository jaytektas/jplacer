// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "machine/JPCell.h"
#include "machine/JPScripting.h"
#include "tasks/JPBackgroundCalibration.h"
#include "tasks/JPBacklashCalibrator.h"
#include "tasks/JPRunoutCalibrator.h"
#include "ui/JPCameraPanel.h"
#include "vision/JPGrayImage.h"

#include <j/app/JAppWindow.h>

#include <functional>
#include <map>
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
    // `finished`: whether it was calibrated (false too when it could not start, or was not confirmed).
    void calibrate(JPCameraPanel& camera, std::function<void(bool ok)> finished = nullptr);
    // Look at the homing mark and say how far it is from its setting.
    void visualTest(JPCameraPanel& camera);
    // Measure the runout of the tip on nozzle `nozzleId` with the fixed
    // camera looking up (JPRunoutCalibrator), asked first (the nozzle goes
    // down to the camera). `done` (main thread): the runout, for the owner to keep.
    // OpenPnP's precise camera <-> nozzle offsets calibration (its Calibration
    // Solutions): a test object (the head's Calibration Rig Test Object, this
    // wide) on the primary fiducial, the camera centred on it; the nozzle
    // picks it at 6 angles round the circle and places it turned 180 degrees,
    // the camera finding it after each. `done` (main thread): how far the
    // nozzle's X and Y offsets are off (the average of where it moved, half
    // its turns' displacement each).
    void calibrateNozzleOffsets(JPCameraPanel& camera, const JPNozzleConfig& nozzle, std::function<void(double, double)> done);
    // OpenPnP's Auto Focus Test: the nozzle (with its tip) over the fixed
    // camera, from its tip's largest part height above the camera's Z down
    // to it, found in focus (JPAutoFocus). `done` (main thread): how far above
    // the camera's Z that was.
    void autoFocusTest(JPCameraPanel& camera, const JPNozzleConfig& nozzle, std::function<void(double)> done);
    // OpenPnP's scripting, for its NozzleCalibration events (none: not run).
    void setScripting(std::shared_ptr<JPScripting> scripting) { m_scripting = std::move(scripting); }
    // A nozzle's tip's runout measured over the fixed camera, asking first
    // (`ask`) or not (an automatic recalibration). `done` (main thread): whether
    // it was measured, the runout, the background calibration's result (with
    // the tip's on; none when too few pictures) and, failing, why.
    using RunoutDone = std::function<void(bool ok, const JPRunout&, const std::optional<JPBackgroundCalibration::Result>&,
                                          const std::string& why)>;
    void calibrateRunout(const std::string& nozzleId, bool ask, RunoutDone done);
    // The measuring itself, on the calling thread (a job's): NozzleCalibration's
    // scripting events round it, the background calibrated along with it.
    static std::optional<JPRunout> measureRunout(JPCell& cell, JPCameraFeed& feed, const JPNozzleConfig& nozzle,
                                                 const JPNozzleTipConfig& tip, JPScripting* scripting, std::string& words,
                                                 const std::function<void(const std::string&)>& progress,
                                                 std::optional<JPBackgroundCalibration::Result>& background);
    // The settling test: a camera on a head moved (dx, dy) and back, or for a
    // fixed camera `tool` (a nozzle held over it, by hand) moved so, then let
    // settle, how it settled kept (JPSettleTrace). `done` (main thread): the trace.
    void settleTest(JPCameraPanel& camera, const JPMountConfig* tool, double dx, double dy,
                    std::function<void(const JPSettleTrace&)> done);
    // Measure an X or Y axis's backlash with the head camera over the head's
    // homing mark (JPBacklashCalibrator). `done` (main thread): what it found,
    // in use already, for the owner to keep.
    void calibrateBacklash(const std::string& axisId, std::function<void(const JPBacklashCalibrator::Result&)> done,
                           std::function<void(bool ok)> finished = nullptr);
    // Finish a home with the camera (JPVisualHoming), through the first
    // calibrated camera on a head that homes visually. Says why not when
    // there is no such camera. Nothing when no head homes visually. `done`
    // (on the main thread): homing is finished, true when it went well (or
    // there was nothing more to do).
    void visualHome(std::function<void(bool)> done = nullptr);

    // The camera the board is worked with: the first one riding on a head,
    // a calibrated one first. Null when no camera rides on a head.
    JPCameraPanel* headCamera() const;
    // A camera's calibration and where it is looking, when it is a calibrated
    // camera on a head (for drawing the machine over its picture).
    bool cameraLook(const std::string& cameraId, JPCameraCalibration& calibration, double& viewX, double& viewY) const;
    // A task is under way.
    bool busy() const { return m_busy; }

private:
    // Runs `task` on the worker with `camera` shown and running; its answer
    // (ok, words) comes back on the main thread to `done`. `progress` from the
    // task is shown as it goes.
    using Task = std::function<bool(std::string& words, const std::function<void(const std::string&)>& progress)>;
    void run(JPCameraPanel& camera, const std::string& name, Task task, std::function<void(bool)> done = nullptr);
    void calibrateFixed(JPCameraPanel& camera, std::function<void(bool ok)> finished);
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
    std::shared_ptr<JPScripting>        m_scripting;
    std::thread                         m_worker;
    bool                                m_busy = false;   // main thread's
    std::shared_ptr<bool>               m_alive = std::make_shared<bool>(true);
};

} // inline namespace jf
