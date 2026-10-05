// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include <string>

inline namespace jf {

// OpenPnP's default pipelines, as its resources hold them: what Reset
// Pipeline puts back.
class JPDefaultPipelines {
public:
    // ReferenceStripFeeder-DefaultPipeline.xml.
    static const std::string& stripFeeder();
};

} // inline namespace jf
