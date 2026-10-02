// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPlacerCameraTasks.h"

#include "job/JPBoard.h"
#include "job/JPBoardSide.h"
#include "ui/JPBoardPanel.h"
#include "ui/JPViewMark.h"

#include <j/app/JAppWindow.h>

#include <functional>
#include <memory>
#include <string>

inline namespace jf {

// The board on the machine and its Board panel: the board read from a
// pick-and-place file, the side up, and where it is (a starting point from
// the camera put on one fiducial, then measured by all of them). Kept in the
// settings (JPlacerSettings::kBoard...), so it is there again next time.
class JPlacerBoard {
public:
    // `square`: correct the machine's squareness by `xPerY` more, for the
    // gantry that moves the camera on `mount` (JPSquarenessConfig).
    using Square = std::function<void(const JPMountConfig& mount, double xPerY)>;

    JPlacerBoard(JAppWindow& window, JPlacerCameraTasks& tasks, Square square);

    // The panel, for the window's dock (made once; this keeps a pointer).
    std::unique_ptr<JPBoardPanel> makePanel(JSceneGraph& graph);
    // Before the panel goes.
    void dropPanel() { m_panel = nullptr; }
    // The board's placements and fiducials on the side up, where camera
    // `cameraId` sees them, once the board has a place (drawn over its picture).
    std::vector<JPViewMark> marks(const std::string& cameraId) const;

private:
    void import();
    bool read(const std::string& path, std::string& error);
    void setSide(bool bottom);
    void cameraOn(const std::string& designator);
    void locate();
    void goTo(const std::string& designator);
    void square();
    void show();
    void save() const;

    JAppWindow&         m_window;
    JPlacerCameraTasks& m_tasks;
    JPBoardPanel*       m_panel = nullptr;
    std::string         m_file;
    JPBoard             m_board;
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
