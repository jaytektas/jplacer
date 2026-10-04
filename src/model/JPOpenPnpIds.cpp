// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPOpenPnpIds.h"

#include <atomic>
#include <chrono>
#include <cstdio>

inline namespace jf {

std::string JPOpenPnpIds::create(const std::string& prefix) {
    static std::atomic<long long> last { 0 };
    long long now = std::chrono::duration_cast<std::chrono::nanoseconds>(
                        std::chrono::system_clock::now().time_since_epoch()).count();
    long long prev = last.load();
    do {
        if (now <= prev) now = prev + 1;
    } while (!last.compare_exchange_weak(prev, now));
    char hex[32];
    std::snprintf(hex, sizeof hex, "%llx", now);
    return prefix + hex;
}

} // inline namespace jf
