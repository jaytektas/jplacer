// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPSource.h"

#include <cstdint>
#include <cstdio>
#include <fstream>

inline namespace jf {

std::string JPSource::fingerprintOf(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return {};
    // FNV-1a, 64 bits: enough to tell a changed file from the same one.
    std::uint64_t h = 1469598103934665603ULL;
    char buf[65536];
    while (in.read(buf, sizeof buf) || in.gcount() > 0) {
        for (std::streamsize i = 0; i < in.gcount(); ++i) {
            h ^= static_cast<unsigned char>(buf[i]);
            h *= 1099511628211ULL;
        }
        if (!in) break;
    }
    char out[17];
    std::snprintf(out, sizeof out, "%016llx", static_cast<unsigned long long>(h));
    return out;
}

} // inline namespace jf
