// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPUuid.h"

#include <cstdint>
#include <cstdio>
#include <mutex>
#include <random>

inline namespace jf {

std::string JPUuid::make() {
    static std::mutex m;
    static std::mt19937_64 rng(std::random_device{}() ^ (uint64_t(std::random_device{}()) << 32));
    uint64_t hi, lo;
    {
        std::lock_guard lk(m);
        hi = rng();
        lo = rng();
    }
    hi = (hi & 0xFFFFFFFFFFFF0FFFull) | 0x0000000000004000ull;   // version 4
    lo = (lo & 0x3FFFFFFFFFFFFFFFull) | 0x8000000000000000ull;   // variant 10
    char buf[40];
    std::snprintf(buf, sizeof buf, "%08x-%04x-%04x-%04x-%012llx", unsigned(hi >> 32), unsigned((hi >> 16) & 0xFFFF),
                  unsigned(hi & 0xFFFF), unsigned(lo >> 48), static_cast<unsigned long long>(lo & 0xFFFFFFFFFFFFull));
    return buf;
}

} // inline namespace jf
