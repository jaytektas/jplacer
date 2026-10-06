// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPJavaRandom.h"

inline namespace jf {

namespace {

constexpr uint64_t kMultiplier = 0x5DEECE66DULL;
constexpr uint64_t kAddend = 0xBULL;
constexpr uint64_t kMask = (1ULL << 48) - 1;

} // namespace

JPJavaRandom::JPJavaRandom(int64_t seed) : m_seed((uint64_t(seed) ^ kMultiplier) & kMask) {}

int JPJavaRandom::next(int bits) {
    m_seed = (m_seed * kMultiplier + kAddend) & kMask;
    return int(int32_t(uint32_t(m_seed >> (48 - bits))));
}

int JPJavaRandom::nextInt(int bound) {
    if ((bound & -bound) == bound) return int((int64_t(bound) * int64_t(next(31))) >> 31);   // a power of two
    int bits, val;
    do {
        bits = next(31);
        val = bits % bound;
    } while (int32_t(uint32_t(bits) - uint32_t(val) + uint32_t(bound - 1)) < 0);   // Java's int overflow: rejected
    return val;
}

double JPJavaRandom::nextDouble() {
    return double((int64_t(next(26)) << 27) + next(27)) * (1.0 / double(1ULL << 53));
}

} // inline namespace jf
