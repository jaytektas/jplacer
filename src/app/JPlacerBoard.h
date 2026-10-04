// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPlacerCameraTasks.h"
#include "JPlacerJob.h"

#include "job/JPBoard.h"
#include "job/JPBoardSide.h"
#include "machine/JPBoardStartConfig.h"
#include "ui/JPBoardPanel.h"
#include "ui/JPViewMark.h"

#include <j/app/JAppWindow.h>

#include <functional>
#include <map>
#include <memory>
#include <string>

inline namespace jf {

// The job's board on the machine and its Board panel: the side up, and where
// it is, from its references (placements marked to locate it by: fiducials,
// or any part the camera can find, JPBoardLocator): found by the camera, or
// fitted to positions recorded by hand (the job's JPLocateSettings).
//
// Before the camera looks for references it needs a first guess, got as the
// machine says (JPBoardStartConfig): by hand (the camera put on one
// reference), from a fixture anchor, or by scanning a region. Where it is is
// kept in the settings (JPlacerSettings::kBoard...), so it is there again
// next time; a new board in the job (another job, a first file read in)
// starts again from nowhere, and homing again, calibrating the camera again
// or squaring the machine mark it as needing locating again.
class JPlacerBoard {
public:
    // `square`: correct the machine's squareness by `xPerY` more, for the
    // gantry that moves the camera on `mount` (JPSquarenessConfig).
    using Square = std::function<void(const JPMountConfig& mount, double xPerY)>;

    JPlacerBoard(JAppWindow& window, JPlacerJob& job, JPlacerCameraTasks& tasks, const JPCellConfig& cell, Square square);
    ~JPlacerBoard();

    // The panel, for the window's dock (made once; this keeps a pointer).
    std::unique_ptr<JPBoardPanel> makePanel(JSceneGraph& graph);
    // Before the panel goes.
    void dropPanel() { m_panel = nullptr; }
    // The board's placements and fiducials on the side up, where camera
    // `cameraId` sees them, once the board has a place (drawn over its picture).
    std::vector<JPViewMark> marks(const std::string& cameraId) const;
    // The machine moved under the board's place (homed again, a camera
    // calibrated again): to be located again before it is trusted.
    void machineChanged(const std::string& why);
    // Where the board is, when it has been located and is still trusted.
    bool located(JPBoardSide& out) const;
    // A footprint drawn over the camera's picture at a placement, turned to
    // `degrees` (its pads outlined, pin 1's dot), until cleared; the camera
    // goes to look at it.
    void showFootprint(const std::string& designator, const JPFootprint& footprint, double degrees);
    void clearFootprint();

private:
    const JPBoard& board() const { return m_job.job().board; }
    void newBoard();
    void setSide(bool bottom);
    void cameraOn(const std::string& designator);
    void record(const std::string& designator);
    void newBoardOnBed();
    void locate();
    void locateFrom(const JPBoardSide& guess);
    void found(const JPBoardLocator::Result& r);
    bool anchorGuess(JPBoardSide& out, std::string& why) const;
    JPlacerCameraTasks::Footprints footprints() const;
    void goTo(const std::string& designator);
    void square();
    void show();
    void save() const;

    JAppWindow&         m_window;
    JPlacerJob&         m_job;
    int                 m_watch = 0;
    JPlacerCameraTasks& m_tasks;
    const JPCellConfig& m_cell;
    JPBoardPanel*       m_panel = nullptr;
    JPBoardSide         m_place;        // where it is, the side up
    bool                m_placed = false;     // m_place says something (else only the side does)
    bool                m_measured = false;   // by its references
    std::string         m_stale;              // why the place is to be measured again (empty: it need not be)
    // What the last locating said of each reference, by designator.
    std::map<std::string, std::string> m_found;
    // The machine's lean as each finding since the board, its side or the
    // squareness last changed measured it (JPBoardLocator::Result::xPerY):
    // one is good to some tens of percent, their mean better.
    std::vector<double> m_leans;
    // The footprint drawn over the picture (empty designator: none).
    std::string         m_shownDesignator;
    JPFootprint         m_shownFootprint;
    double              m_shownDegrees = 0;
    double meanLean() const;
    Square              m_square;
    std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);
};

} // inline namespace jf
