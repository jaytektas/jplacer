// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPChoiceRow.h"

#include <j/core/JButton.h>
#include <j/core/JComboBox.h>
#include <j/core/JContainer.h>
#include <j/core/JLabel.h>
#include <j/core/JListView.h>

#include <functional>
#include <string>
#include <vector>

inline namespace jf {

// The board on the machine: its pick-and-place file, which side is up, a
// starting point (the camera put on one of its fiducials), finding it by its
// fiducials, and going to any placement on it. A view: what it shows comes
// from its owner, what is asked of it goes to the owner's callbacks.
class JPBoardPanel : public JContainer {
public:
    explicit JPBoardPanel(JSceneGraph& graph);

    std::function<void()>                   onImport;
    std::function<void(bool bottom)>        onSide;
    std::function<void(const std::string&)> onCameraOnFiducial;   // the fiducial chosen
    std::function<void()>                   onLocate;
    std::function<void(const std::string&)> onGoTo;               // a designator

    // What the board is (empty name: none); the side up; the up side's
    // fiducials; every placement on the up side, as "R1  0402  10k".
    void showBoard(const std::string& summary, bool bottom, const std::vector<std::string>& fiducials,
                   const std::vector<std::string>& placements, const std::vector<std::string>& designators);
    // Where it is, in words, and how that was found.
    void showPlace(const std::string& text);
    // The last finding: one line a fiducial.
    void showFound(const std::vector<std::string>& lines);
    // A task under way: its buttons off.
    void setBusy(bool busy);

private:
    JLabel*                  m_summary = nullptr;
    JPChoiceRow*             m_side = nullptr;
    JComboBox*               m_fiducial = nullptr;
    JLabel*                  m_place = nullptr;
    JListView*               m_found = nullptr;
    JListView*               m_placements = nullptr;
    std::vector<JButton*>    m_buttons;
    std::vector<std::string> m_designators;
    bool                     m_updating = false;
};

} // inline namespace jf
