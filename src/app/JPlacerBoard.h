// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPlacerCameraTasks.h"

#include "job/JPBoard.h"
#include "job/JPBoardSide.h"
#include "ui/JPBoardPanel.h"

#include <j/app/JAppWindow.h>

#include <memory>
#include <string>

inline namespace jf {

// The board on the machine and its Board panel: the board read from a
// pick-and-place file, the side up, and where it is (a starting point from
// the camera put on one fiducial, then measured by all of them). Kept in the
// settings (JPlacerSettings::kBoard...), so it is there again next time.
class JPlacerBoard {
public:
    JPlacerBoard(JAppWindow& window, JPlacerCameraTasks& tasks);

    // The panel, for the window's dock (made once; this keeps a pointer).
    std::unique_ptr<JPBoardPanel> makePanel(JSceneGraph& graph);
    // Before the panel goes.
    void dropPanel() { m_panel = nullptr; }

private:
    void import();
    bool read(const std::string& path, std::string& error);
    void setSide(bool bottom);
    void cameraOn(const std::string& designator);
    void locate();
    void goTo(const std::string& designator);
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
    std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);
};

} // inline namespace jf
