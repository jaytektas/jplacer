// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

inline namespace jf {

// How a camera came to rest once, for the settling graph: each picture's
// difference from the one before (percent of full scale, by the method)
// against how long after the wait began it was taken; the threshold the
// method waits for; and when it was taken to be still (-1: it was not, and
// the wait timed out; with FixedTime, when the fixed time was up).
struct JPSettleTrace {
    std::string                            method;
    double                                 threshold = 0;
    std::vector<std::pair<double, double>> points;   // (ms, difference %)
    double                                 settledMs = -1;
    // With the camera's Diagnostics: when each picture was being taken
    // (ms, 1 taking, 0 not), and the pictures as compared (OpenPnP's replay),
    // grey or colour, one byte a channel, by when each was taken.
    struct Picture {
        double               ms = 0;
        int                  width = 0, height = 0, channels = 1;
        std::vector<uint8_t> pixels;
    };
    std::vector<std::pair<double, double>>       captures;
    std::shared_ptr<const std::vector<Picture>>  pictures;
    int                                          replay = -1;   // the picture shown (none: -1)
};

} // inline namespace jf
