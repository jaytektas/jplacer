// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPlacerCameraTasks.h"
#include "JPlacerJob.h"

#include "job/JPBoard.h"
#include "job/JPBoardSide.h"
#include "ui/JPBoardPanel.h"
#include "ui/JPViewMark.h"

#include <j/app/JAppWindow.h>

#include <functional>
#include <memory>
#include <string>

inline namespace jf {

// The job's board on the machine and its Board panel: the side up, and where
// it is (a starting point from the camera put on one fiducial, then measured
// by all of them). Where it is is kept in the settings
// (JPlacerSettings::kBoard...), so it is there again next time; a new board
// in the job (another job, a file read in) starts again from nowhere.
class JPlacerBoard {
public:
    // `square`: correct the machine's squareness by `xPerY` more, for the
    // gantry that moves the camera on `mount` (JPSquarenessConfig).
    using Square = std::function<void(const JPMountConfig& mount, double xPerY)>;

    JPlacerBoard(JAppWindow& window, JPlacerJob& job, JPlacerCameraTasks& tasks, Square square);
    ~JPlacerBoard();

    // The panel, for the window's dock (made once; this keeps a pointer).
    std::unique_ptr<JPBoardPanel> makePanel(JSceneGraph& graph);
    // Before the panel goes.
    void dropPanel() { m_panel = nullptr; }
    // The board's placements and fiducials on the side up, where camera
    // `cameraId` sees them, once the board has a place (drawn over its picture).
    std::vector<JPViewMark> marks(const std::string& cameraId) const;

private:
    const JPBoard& board() const { return m_job.job().board; }
    void newBoard();
    void setSide(bool bottom);
    void cameraOn(const std::string& designator);
    void locate();
    void goTo(const std::string& designator);
    void square();
    void show();
    void save() const;

    JAppWindow&         m_window;
    JPlacerJob&         m_job;
    int                 m_watch = 0;
    JPlacerCameraTasks& m_tasks;
    JPBoardPanel*       m_panel = nullptr;
    JPBoardSide         m_place;        // where it is, the side up
    bool                m_placed = false;     // m_place says something (else only the side does)
    bool                m_measured = false;   // by its fiducials
    std::vector<std::string> m_found;   // the last finding, a line a fiducial
    // The machine's lean as each finding since the board, its side or the
    // squareness last changed measured it (JPBoardLocator::Result::xPerY):
    // one is good to some tens of percent, their mean better.
    std::vector<double> m_leans;
    double meanLean() const;
    Square              m_square;
    std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);
};

} // inline namespace jf
