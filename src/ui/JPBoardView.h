// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "job/JPBoard.h"
#include "job/JPBoardFrame.h"

#include <j/core/JWidget.h>

inline namespace jf {

// A drawing of a board as its data puts it, seen from the top: the outline
// (dashed where there is none and the placements' extent stands in for it),
// the origin, each placement a dot (filled on the top, hollow on the bottom),
// and each fiducial a ring. Fitted to the view, Y up. What is wrong with an
// import shows here first: a part off the board, an origin in the middle, a
// side mirrored the wrong way.
class JPBoardView : public JWidget {
public:
    explicit JPBoardView(JSceneGraph& graph);

    void show(const JPBoard& board, const JPBoardFrame& frame);

    void populateRenderPrimitives(JPrimitiveBuffer& buf) override;

private:
    JPBoard      m_board;
    JPBoardFrame m_frame;
};

} // inline namespace jf
