// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

inline namespace jf {

// Where the board's origin is and what its outline is, set when it is read
// in. The origin is the board's bottom-left corner: `originX`, `originY` are
// where it is in the file's own coordinates (0, 0: the file's origin), and
// every placement is moved by it. The outline, where there is one, is a
// rectangle `width` by `height` from the origin; without one, the board is
// taken to be the placements' extent (drawn dashed: a guess).
struct JPBoardFrame {
    double originX = 0, originY = 0;
    double width = 0, height = 0;   // 0: no outline

    bool hasOutline() const { return width > 0 && height > 0; }
};

} // inline namespace jf
