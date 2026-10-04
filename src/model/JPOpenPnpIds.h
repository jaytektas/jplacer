// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include <string>

inline namespace jf {

// OpenPnP's Configuration.createId: a prefix ("FDR", "BVS", "FVS") and the
// time in nanoseconds, in hex; never the same twice.
class JPOpenPnpIds {
public:
    static std::string create(const std::string& prefix);
};

} // inline namespace jf
