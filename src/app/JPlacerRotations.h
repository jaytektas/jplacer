// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPlacerJob.h"
#include "JPlacerLayout.h"
#include "JPlacerMachine.h"

#include "ui/JPPartsPanel.h"

#include <j/core/DockWidget.h>

#include <deque>
#include <map>
#include <memory>
#include <string>

inline namespace jf {

// The Rotations dock: the job's rotation check (JPRotationCheck), one rule
// of its checklist (the job's JPRotationRules). Each package the job places
// is listed with how its turn can be settled and whether it is; Check
// Rotations runs the ways the rule allows, cheapest first (cannot matter,
// the file's pad 1, vision on the board), and leaves what only the person
// can settle. Check on Board draws the chosen package's footprint over the
// camera's picture of its first placement (JPlacerBoard::showFootprint), to
// turn a quarter at a time until pad 1 sits on the board's pin-1 mark and
// accept: that sets the package's turn, and it is checked.
class JPlacerRotations {
public:
    static constexpr const char* kTitle = "Rotations";

    JPlacerRotations(JPlacerJob& job, JPlacerMachine& machine, JSceneGraph& graph, JPlacerLayout& layout);
    ~JPlacerRotations();

    void showDock();

private:
    // The job's packages that are placed, each with its placements.
    std::map<std::string, std::vector<const JPPlacement*>> packages() const;
    void show();
    void page();
    void checkAll();
    void nextVision();
    void onlyVision(const std::string& packageId);
    void onBoard(int quarters);   // 0: start; else turn the drawn footprint so many quarters
    void accept();
    void cancel();
    void onAction(const std::string& key);
    void onChoice(const std::string& key, int index);
    void edited();

    JPlacerJob&                   m_job;
    JPlacerMachine&               m_machine;
    JPlacerLayout&                m_layout;
    int                           m_watch = 0;
    std::unique_ptr<JPPartsPanel> m_panel;
    std::unique_ptr<JDockWidget>  m_dock;
    std::string                   m_chosen;      // a package id
    std::map<std::string, std::string> m_notes;  // by package: what the last check said
    std::map<std::string, int>    m_fileSays;    // by package: the quarter turns the file says it is out
    std::deque<std::string>       m_visionQueue; // packages waiting for the camera
    // The camera check under way: the package, the placement it is drawn at, the turn being tried.
    std::string                   m_checking, m_checkingAt;
    double                        m_pendingTurn = 0;
    std::shared_ptr<bool>         m_alive = std::make_shared<bool>(true);
};

} // inline namespace jf
