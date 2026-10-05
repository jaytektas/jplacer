// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "model/JPLocation.h"

#include <optional>
#include <vector>

inline namespace jf {

// OpenPnP's TravellingSalesman: an order through some places from `start`
// (and towards `end`), short: nearest first, then improved by 2-opt.
class JPTravel {
public:
    static std::vector<size_t> order(const std::vector<JPLocation>& points, const std::optional<JPLocation>& start,
                                     const std::optional<JPLocation>& end);
};

} // inline namespace jf
