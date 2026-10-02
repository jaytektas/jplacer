// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include <string>
#include <vector>

inline namespace jf {

// What a controller said to one command: whether it was accepted, the lines
// it sent before accepting or refusing it, and why it failed (the
// controller's own error, a timeout, or a lost connection).
struct JPReply {
    bool                     ok = false;
    std::vector<std::string> lines;
    std::string              error;
};

} // inline namespace jf
