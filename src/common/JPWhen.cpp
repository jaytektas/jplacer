// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPWhen.h"

#include <cstdio>
#include <ctime>

inline namespace jf {

namespace {

constexpr long kMinute = 60, kHour = 60 * kMinute, kDay = 24 * kHour, kMonth = 30 * kDay, kYear = 365 * kDay;

std::string count(long n, const char* unit) {
    return std::to_string(n) + " " + unit + (n == 1 ? "" : "s") + " ago";
}

} // namespace

std::string JPWhen::now() {
    const std::time_t t = std::time(nullptr);
    char buf[32];
    std::strftime(buf, sizeof buf, "%Y-%m-%d %H:%M:%S", std::localtime(&t));
    return buf;
}

std::string JPWhen::ago(const std::string& when) {
    std::tm tm {};
    if (std::sscanf(when.c_str(), "%d-%d-%d %d:%d:%d", &tm.tm_year, &tm.tm_mon, &tm.tm_mday, &tm.tm_hour, &tm.tm_min, &tm.tm_sec) < 5)
        return {};
    tm.tm_year -= 1900;
    tm.tm_mon -= 1;
    tm.tm_isdst = -1;
    const std::time_t then = std::mktime(&tm);
    if (then == -1) return {};
    const long s = long(std::difftime(std::time(nullptr), then));
    if (s < kMinute) return "just now";
    if (s < kHour) return count(s / kMinute, "minute");
    if (s < kDay) return count(s / kHour, "hour");
    if (s < kMonth) return count(s / kDay, "day");
    if (s < kYear) return count(s / kMonth, "month");
    return count(s / kYear, "year");
}

std::string JPWhen::withAgo(const std::string& when) {
    if (when.empty()) return "never";
    const std::string a = ago(when);
    // To the minute: the seconds are of no use to whoever reads it.
    return a.empty() ? when : when.substr(0, 16) + " (" + a + ")";
}

} // inline namespace jf
