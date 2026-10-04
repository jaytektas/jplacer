// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPPlacementsViewerCanvas.h"

#include <j/core/JButton.h>
#include <j/core/JCheckBox.h>
#include <j/core/JComboBox.h>
#include <j/core/JContainer.h>
#include <j/core/JLabel.h>

#include <utility>
#include <vector>

inline namespace jf {

// OpenPnP's board and panel viewer (PlacementsHolderLocationViewer): the
// drawing (JPPlacementsViewerCanvas) and, at its right, Viewing From Top
// (or Bottom), for a panel what is shown (its children only or all its
// descendants), Reticle, Board/Panel Locations, Board/Panel Origins,
// Fiducials and Placements, and the hints. A board starts with all of them
// but the reticle shown; a panel with its locations only.
class JPPlacementsViewer : public JContainer {
public:
    JPPlacementsViewer(JSceneGraph& graph, JPPlacementsHolderLocation* root, bool isJob);

    JPPlacementsViewerCanvas& canvas() { return *m_canvas; }
    // Another board or panel to show (the tab's chosen one).
    void setRoot(JPPlacementsHolderLocation* root);
    void regenerate() { m_canvas->regenerate(); }
    // One of what is shown set (its tick box with it).
    void setShown(bool JPPlacementsViewerCanvas::*flag, bool on);
    // The name shown over the options (the board's or panel's).
    void setName(const std::string& name) { m_name->setText(name); }

private:
    void viewingSideChanged();

    JPPlacementsViewerCanvas* m_canvas = nullptr;
    JLabel*                   m_name = nullptr;
    JButton*                  m_side = nullptr;
    JComboBox*                m_viewing = nullptr;
    bool                      m_isJob;
    std::vector<std::pair<JCheckBox*, bool JPPlacementsViewerCanvas::*>> m_options;
    bool                                   m_shown = false;   // a board or panel shown yet
    JPPlacementsHolderLocation::Kind       m_shownKind = JPPlacementsHolderLocation::Kind::Board;
};

} // inline namespace jf
