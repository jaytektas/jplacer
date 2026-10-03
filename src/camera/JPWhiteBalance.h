// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPFrame.h"

#include "machine/JPCameraConfig.h"

#include <array>
#include <cstdint>
#include <optional>
#include <string>

inline namespace jf {

// A camera's white balance as OpenPnP does it: each colour channel scaled by
// its balance, then given its own gamma (out = (in / 255 * balance) ^ (1 /
// gamma)), through a lookup table on every picture.
class JPWhiteBalance {
public:
    using Values = JPCameraConfig::WhiteBalance;

    explicit JPWhiteBalance(const Values& v);

    // Every pixel of `frame` (RGBA) through the tables; nothing to do when neutral.
    void apply(JPFrame& frame) const;

    // OpenPnP's automatic balance from a picture taken without one: the other
    // channels brought up to the one with the most signal, measured as the
    // mean above the 80th percentile (`averaged`, "Overall") or that
    // percentile itself ("Brightest"), and the gammas matched at the median.
    // Nothing, with `why`, when the picture is too dark to tell.
    static std::optional<Values> automatic(const JPFrame& frame, bool averaged, std::string& why);

private:
    bool                                  m_neutral;
    std::array<std::array<uint8_t, 256>, 3> m_table;
};

} // inline namespace jf
