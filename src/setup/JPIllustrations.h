// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "camera/JPFrame.h"

#include <memory>
#include <string>

inline namespace jf {

// OpenPnP's illustrations (illustrations/ beside the executable; in a build,
// the source tree's), each read once, laid on white (they are drawn for a light page).
class JPIllustrations {
public:
    // The picture of that file name ("rotatedtrayfeeder.png"); null, logged, when it cannot be read.
    static std::shared_ptr<const JPFrame> picture(const std::string& fileName);
};

} // inline namespace jf
