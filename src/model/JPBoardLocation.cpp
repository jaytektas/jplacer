// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPBoardLocation.h"

inline namespace jf {

std::unique_ptr<JPPlacementsHolderLocation> JPBoardLocation::instance() const {
    return std::unique_ptr<JPPlacementsHolderLocation>(new JPBoardLocation(*this));
}

} // inline namespace jf
