// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// JPWhen: a calibration's when, and how long ago, in words.
// Tests check with assert(); a Release build must not compile it away.
#undef NDEBUG
#include <cassert>

#include "common/JPWhen.h"

#include <ctime>
#include <string>

using namespace jf;

namespace {
std::string at(long secondsAgo) {
    const std::time_t t = std::time(nullptr) - secondsAgo;
    char buf[32];
    std::strftime(buf, sizeof buf, "%Y-%m-%d %H:%M:%S", std::localtime(&t));
    return buf;
}
} // namespace

int main() {
    assert(JPWhen::ago(at(5)) == "just now");
    assert(JPWhen::ago(at(3 * 3600 + 5)) == "3 hours ago");
    assert(JPWhen::ago(at(86400 + 60)) == "1 day ago");
    assert(JPWhen::ago(at(65L * 86400)) == "2 months ago");
    assert(JPWhen::withAgo("") == "never");
    assert(JPWhen::withAgo("measured in OpenPnP") == "measured in OpenPnP");
    const std::string w = JPWhen::withAgo(at(2 * 86400 + 60));
    assert(w.size() > 16 && w.find("(2 days ago)") != std::string::npos);
    return 0;
}
