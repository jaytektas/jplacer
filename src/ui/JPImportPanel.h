// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPBoardView.h"
#include "JPTextField.h"

#include <j/core/JButton.h>
#include <j/core/JComboBox.h>
#include <j/core/JContainer.h>
#include <j/core/JLabel.h>
#include <j/core/JListView.h>

#include <functional>
#include <string>
#include <vector>

inline namespace jf {

// A board's sources and the review of reading them: a bar saying what is
// waiting to be accepted (Accept, Discard); the sources (the pick-and-place
// file and the CAD tool it is read as, the BOM), each chosen, read again or
// taken away; the origin and the outline; a drawing of the board as it
// would be (JPBoardView); and the report beside it. A view: what it shows
// comes from its owner, what is asked of it goes to the owner's callbacks.
class JPImportPanel : public JContainer {
public:
    explicit JPImportPanel(JSceneGraph& graph, const std::vector<std::string>& tools);

    struct Source {
        std::string file;      // shown; empty: none
        std::string state;     // "changed on disk since it was read", or empty
        bool        canReread = false;
    };
    void showPending(const std::string& text, bool canAccept, bool canDiscard);
    void showSources(const Source& placements, int tool, const Source& bom);
    void showFrame(double originX, double originY, double width, double height);
    void showBoard(const JPBoard& board, const JPBoardFrame& frame);
    // The report, one line each; `activatable` lines say what a double-click does.
    void showReport(const std::vector<std::string>& lines);

    std::function<void()>           onAccept, onDiscard;
    std::function<void()>           onChoosePlacements, onRereadPlacements;
    std::function<void(int tool)>   onTool;
    std::function<void()>           onChooseBom, onRereadBom, onRemoveBom;
    std::function<void(double x, double y)> onOrigin;
    std::function<void()>           onOriginAtParts;
    std::function<void(double w, double h)> onOutline;
    std::function<void()>           onOutlineAroundParts, onNoOutline;
    std::function<void(int line)>   onReportLine;   // double-clicked

private:
    JLabel*      m_pending = nullptr;
    JButton*     m_accept = nullptr;
    JButton*     m_discard = nullptr;
    JLabel*      m_placementsFile = nullptr;
    JLabel*      m_placementsState = nullptr;
    JComboBox*   m_tool = nullptr;
    JButton*     m_rereadPlacements = nullptr;
    JLabel*      m_bomFile = nullptr;
    JLabel*      m_bomState = nullptr;
    JButton*     m_rereadBom = nullptr;
    JButton*     m_removeBom = nullptr;
    JPTextField* m_originX = nullptr;
    JPTextField* m_originY = nullptr;
    JPTextField* m_width = nullptr;
    JPTextField* m_height = nullptr;
    JPBoardView* m_board = nullptr;
    JListView*   m_report = nullptr;
    bool         m_updating = false;
};

} // inline namespace jf
