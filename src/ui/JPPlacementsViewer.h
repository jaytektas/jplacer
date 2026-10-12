// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPPlacementsViewerCanvas.h"

#include <j/core/JButton.h>
#include <j/core/JCheckBox.h>
#include <j/core/JComboBox.h>
#include <j/core/JContainer.h>
#include <j/core/JLabel.h>

#include <memory>
#include <utility>
#include <vector>

inline namespace jf {

// OpenPnP's board and panel viewer (PlacementsHolderLocationViewer): the
// drawing (JPPlacementsViewerCanvas) and, at its right, Viewing From Top
// (or Bottom), for a panel what is shown (its children only or all its
// descendants), Reticle, Board/Panel Locations, Board/Panel Origins,
// Fiducials and Placements, and the hints. A board starts with all of them
// but the reticle shown; a panel with its locations only. A board's viewer
// lists its photos (JPBoardPhoto, the newest first): the one chosen is shown
// under the drawing (from the side that was up), what was said of it below
// the list, and Delete Photo… deletes it.
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
    // Where the photos' textures are made.
    void setHal(JGpuHal* hal) { m_canvas->setHal(hal); }
    // The board's photos listed again (one taken or deleted), the one shown kept where it still is.
    void photosChanged();

private:
    void viewingSideChanged();
    void showPhoto(int index);
    void deletePhoto();

    JPPlacementsViewerCanvas* m_canvas = nullptr;
    JLabel*                   m_name = nullptr;
    JButton*                  m_side = nullptr;
    JComboBox*                m_viewing = nullptr;
    bool                      m_isJob;
    std::vector<std::pair<JCheckBox*, bool JPPlacementsViewerCanvas::*>> m_options;
    bool                                   m_shown = false;   // a board or panel shown yet
    JPPlacementsHolderLocation::Kind       m_shownKind = JPPlacementsHolderLocation::Kind::Board;
    // The board's photos.
    JLabel*                                m_photoLabel = nullptr;
    JComboBox*                             m_photos = nullptr;
    JButton*                               m_deletePhoto = nullptr;
    JLabel*                                m_photoNotes = nullptr;
    float                                  m_inner = 0;   // the options' width, for the notes to wrap to
    std::string                            m_boardFile;
    std::vector<JPBoardPhoto>              m_photoList;
    std::string                            m_shownPhoto;   // its file; empty: none
    std::shared_ptr<bool>                  m_alive = std::make_shared<bool>(true);
};

} // inline namespace jf
