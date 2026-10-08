// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "machine/JPCameraCalibration.h"

#include <optional>
#include <vector>

inline namespace jf {

// A camera's picture straightened for looking at: the lens's bending taken
// out, and the machine square to the picture at one scale both ways (as a
// camera looking that way and mounted straight would see it), centred on the
// point the camera looks at. On it the machine is drawn with plain geometry: a
// footprint is a scaled, moved copy of itself. Vision pipelines are given it
// too (JPStraightPicture); jplacer's own finders measure on the picture as
// taken, through the lens model.
//
// Straightened, a barrel picture no longer fills a rectangle. `showAll`
// chooses how much is shown: 0 enlarges it until every pixel has picture
// behind it, 1 shows all the camera sees with bare corners, and between them
// is between.
//
// Drawn as a mesh: a grid over the straightened picture, each node knowing
// where in the picture as taken it comes from; drawn by the GPU, or by the
// software renderer where there is none.
class JPStraightener {
public:
    // A grid node: where it is in the picture as taken (0..1 of its size), and
    // whether the camera sees there at all.
    struct Node { float u = 0, v = 0; bool seen = false; };

    // Nothing when the calibration cannot place pixels (not valid, no size).
    static std::optional<JPStraightener> make(const JPCameraCalibration& calibration, bool lookingUp, double showAll);

    int width() const { return m_w; }
    int height() const { return m_h; }
    // The grid: (columns + 1) x (rows + 1) nodes over the straightened
    // picture, row by row; cell (c, r) spans width / columns by height / rows.
    int columns() const { return kColumns; }
    int rows() const { return kRows; }
    const std::vector<Node>& grid() const { return m_grid; }

    // A pixel of the picture as taken to where it is in the straightened one,
    // and back. False where there is none.
    bool toStraight(double rawX, double rawY, double& x, double& y) const;
    // Straightened pixels per mm of the camera's move, along the picture's x and y (signed: looking down, x
    // runs against the machine's X).
    double scaleX() const { return m_scaleX; }
    double scaleY() const { return m_scaleY; }
    bool toRaw(double x, double y, double& rawX, double& rawY) const;

private:
    // Fine enough that a straight line between nodes strays from the lens's
    // curve by far less than a pixel.
    static constexpr int kColumns = 64, kRows = 36;

    JPStraightener() = default;

    JPCameraCalibration m_cal;
    JPLens              m_lens;
    double              m_u0x = 0, m_u0y = 0;   // the picture's middle, straightened through the lens
    double              m_scaleX = 0, m_scaleY = 0;   // straightened px per mm along the picture's x and y
    int                 m_w = 0, m_h = 0;
    std::vector<Node>   m_grid;
};

} // inline namespace jf
