// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPChoiceRow.h"

#include <j/core/JButton.h>
#include <j/core/JContainer.h>
#include <j/core/JLabel.h>
#include <j/core/JListView.h>

#include <functional>
#include <string>
#include <vector>

inline namespace jf {

// The job's board on the machine: which side is up, how it is located
// (references found by the camera, or recorded by hand), its references
// (each with what was last captured of it), the starting point (the camera
// put on one reference), locating it, squaring the machine by it, and going
// to any placement on it. A view: what it shows comes from its owner, what
// is asked of it goes to the owner's callbacks.
class JPBoardPanel : public JContainer {
public:
    explicit JPBoardPanel(JSceneGraph& graph);

    std::function<void()>                   onImport;
    std::function<void(bool bottom)>        onSide;
    std::function<void(bool byHand)>        onCapture;            // how references are captured
    std::function<void(bool reuse)>         onNewBoard;           // by hand: a new board's references reused
    std::function<void()>                   onNewBoardOnBed;      // a new board has been put on the bed
    std::function<void(const std::string&)> onGoTo;               // a designator: the camera to it
    std::function<void(const std::string&)> onRecord;             // the camera is on this reference
    std::function<void(const std::string&)> onCameraOnReference;  // the starting point: the camera is on it
    std::function<void()>                   onLocate;
    std::function<void()>                   onSquare;             // correct the machine's squareness by the board

    // What the board is (empty: none); the side up; the up side's references,
    // one line each (designator first) and their designators; every placement
    // on the up side, as "R1  0402  10k".
    void showBoard(const std::string& summary, bool bottom, const std::vector<std::string>& references,
                   const std::vector<std::string>& referenceKeys, const std::vector<std::string>& placements,
                   const std::vector<std::string>& designators);
    // Captured by hand, and (by hand) whether a new board's references are reused.
    void showCapture(bool byHand, bool reuse);
    // Where it is, in words, and how that was found.
    void showPlace(const std::string& text);
    // A task under way: its buttons off.
    void setBusy(bool busy);
    // What the findings say of the machine's squareness (empty: nothing yet),
    // and so whether it can be squared by them.
    void showLean(const std::string& text);
    // The reference chosen in the list (empty: none), and choosing one.
    std::string chosenReference() const;
    void chooseReference(const std::string& designator);

private:
    JLabel*                  m_summary = nullptr;
    JPChoiceRow*             m_side = nullptr;
    JPChoiceRow*             m_capture = nullptr;
    JPChoiceRow*             m_newBoard = nullptr;
    JLabel*                  m_newBoardLabel = nullptr;
    JListView*               m_references = nullptr;
    JLabel*                  m_place = nullptr;
    JListView*               m_placements = nullptr;
    JButton*                 m_square = nullptr;
    JLabel*                  m_lean = nullptr;
    std::vector<JButton*>    m_buttons;
    bool                     m_canSquare = false;
    std::vector<std::string> m_designators;
    std::vector<std::string> m_referenceKeys;
    bool                     m_updating = false;
};

} // inline namespace jf
