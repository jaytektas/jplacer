// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include <cstdint>

inline namespace jf {

// Java's java.util.Random, bit for bit (its 48-bit linear congruential generator, nextInt and nextDouble as the
// JDK has them): what OpenPnP seeds to make its results repeatable (the travelling salesman's annealing), so the
// same seed gives the same results here.
class JPJavaRandom {
public:
    explicit JPJavaRandom(int64_t seed);

    int    nextInt(int bound);   // 0 <= n < bound (bound > 0)
    double nextDouble();         // [0, 1)

private:
    int next(int bits);

    uint64_t m_seed;
};

} // inline namespace jf
