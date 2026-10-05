// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPLogLine.h"

#include <chrono>
#include <cstdio>
#include <ctime>

inline namespace jf {

const char* JPLogLine::levelName(JLogLevel l) {
    switch (l) {
        case JLogLevel::Trace: return "TRACE";
        case JLogLevel::Debug: return "DEBUG";
        case JLogLevel::Info:  return "INFO";
        case JLogLevel::Warn:  return "WARNING";
        case JLogLevel::Error: return "ERROR";
        case JLogLevel::Off:   return "OFF";
    }
    return "";
}

std::string JPLogLine::text(JLogLevel level, const std::string& category, const std::string& message) {
    const auto now = std::chrono::system_clock::now();
    const std::time_t t = std::chrono::system_clock::to_time_t(now);
    const int ms = int(std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() % 1000);
    std::tm tm {};
    localtime_r(&t, &tm);
    char stamp[40];
    std::snprintf(stamp, sizeof stamp, "%04d-%02d-%02d %02d:%02d:%02d.%03d", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                  tm.tm_hour, tm.tm_min, tm.tm_sec, ms);
    return std::string(stamp) + " " + category + " " + levelName(level) + ": " + message;
}

} // inline namespace jf
