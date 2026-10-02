// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPPlacement.h"

#include "geometry/JPAffine2D.h"

inline namespace jf {

// A board on the machine, one side up: where its CAD coordinates for that
// side land on the machine. The bottom side is seen mirrored (the board
// turned over about its Y axis), so its x runs the other way.
struct JPBoardSide {
    JPPlacement::Side side = JPPlacement::Side::Top;
    JPAffine2D        toMachine;   // board (x, y) of that side -> machine (X, Y)

    // The board placed with its origin at (x, y), turned by `degrees`, `side` up.
    static JPBoardSide placed(JPPlacement::Side side, double x, double y, double degrees) {
        JPBoardSide b;
        b.side = side;
        const JPAffine2D place = JPAffine2D::placed(x, y, degrees);
        b.toMachine = side == JPPlacement::Side::Bottom ? place.after(JPAffine2D::mirrorX()) : place;
        return b;
    }
};

} // inline namespace jf
