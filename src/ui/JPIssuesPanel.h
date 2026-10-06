// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPIconButton.h"
#include "JPSetupForm.h"
#include "JPTable.h"

#include "setup/JPSolutions.h"

#include <j/core/JButton.h>
#include <j/core/JCheckBox.h>
#include <j/core/JContainer.h>
#include <j/core/JLabel.h>
#include <j/core/Splitter.h>

#include <array>
#include <optional>
#include <functional>
#include <memory>
#include <string>
#include <vector>

inline namespace jf {

// The Issues & Solutions tab, as OpenPnP's IssuesAndSolutionsPanel: Find
// Issues & Solutions, the target Milestone (and what it is for, its wiki
// page), Include Solved? and Include Dismissed?; the issues found (Subject,
// Severity, Issue, Solution, State, coloured by severity and state); and the
// one chosen in full (its subject, issue and solution, more about it, what
// it offers to set, its choices) with Accept, Dismiss, Reopen and its wiki
// page. After a state changes, a reminder to search again.
class JPIssuesPanel : public JContainer {
public:
    // `split`: the table's share of the height, as last left.
    JPIssuesPanel(JSceneGraph& graph, JPSolutions& solutions, double split);
    ~JPIssuesPanel() override;

    // Search and show (OpenPnP's findIssuesAndSolutions); the first chosen.
    void findIssuesAndSolutions();
    double split() const;

    // A web page (an issue's or the milestone's wiki page) to open.
    std::function<void(const std::string& uri)> openUri;
    // OpenPnP's issue indicator: the colour of the severest open issue above Information (its severity's
    // colour, saturated), or none; told after each search and each change of state.
    std::function<void(std::optional<std::array<uint8_t, 4>> color)> onIndicator;
    // Something went wrong doing a solution: to be said.
    std::function<void(const std::string& why)> showError;
    // The indicator worked out again and told (onIndicator).
    void updateIndicator();

private:
    class Model;
    std::vector<JPSolutions::Issue*> selections() const;
    void selectionChanged();
    void setState(JPSolutions::State state);
    void showMilestone();
    void showIssue();

    JPSolutions&               m_solutions;
    std::unique_ptr<Model>     m_model;
    JPTable*                   m_table = nullptr;
    JSplitter*                 m_split = nullptr;
    std::unique_ptr<JContainer> m_tablePane, m_issuePane;
    JPSetupForm*               m_form = nullptr;
    JLabel*                    m_milestone = nullptr;
    JLabel*                    m_milestoneText = nullptr;
    JLabel*                    m_warn = nullptr;
    JCheckBox*                 m_showSolved = nullptr;
    JCheckBox*                 m_showDismissed = nullptr;
    JButton*                   m_accept = nullptr;
    JButton*                   m_dismiss = nullptr;
    JButton*                   m_reopen = nullptr;
    JPIconButton*              m_info = nullptr;
    std::shared_ptr<bool>      m_alive = std::make_shared<bool>(true);
};

} // inline namespace jf
