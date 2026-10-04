// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPLogLevels.h"

#include "JPlacerLog.h"

#include <algorithm>
#include <cctype>
#include <set>
#include <sstream>

inline namespace jf {

namespace {

std::string trimmed(std::string s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.erase(s.begin());
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.pop_back();
    return s;
}

bool levelNamed(const std::string& text, JLogLevel& out) {
    for (JLogLevel l : JPLogLevels::choices())
        if (text == JPLogLevels::name(l)) {
            out = l;
            return true;
        }
    return false;
}

} // namespace

const std::vector<JLogLevel>& JPLogLevels::choices() {
    static const std::vector<JLogLevel> all = { JLogLevel::Off, JLogLevel::Error, JLogLevel::Warn, JLogLevel::Info,
                                                JLogLevel::Debug, JLogLevel::Trace };
    return all;
}

std::string JPLogLevels::name(JLogLevel level) {
    switch (level) {
        case JLogLevel::Trace: return "trace";
        case JLogLevel::Debug: return "debug";
        case JLogLevel::Info:  return "info";
        case JLogLevel::Warn:  return "warn";
        case JLogLevel::Error: return "error";
        case JLogLevel::Off:   return "off";
    }
    return "info";
}

std::vector<std::string> JPLogLevels::categories() {
    std::set<std::string> all(JPlacerLog::all().begin(), JPlacerLog::all().end());
    for (const std::string& c : JLog::instance().categories()) all.insert(c);
    return { all.begin(), all.end() };
}

JPLogLevels JPLogLevels::current() {
    JPLogLevels l;
    l.global = JLog::instance().globalLevel();
    for (const std::string& c : categories()) {
        JLogLevel own;
        if (JLog::instance().ownLevel(c, own)) l.own[c] = own;
    }
    return l;
}

void JPLogLevels::apply() const {
    JLog& log = JLog::instance();
    log.setGlobalLevel(global);
    for (const std::string& c : categories())
        if (!own.count(c)) log.clearLevel(c);
    for (const auto& [c, level] : own) log.setLevel(c, level);
}

std::string JPLogLevels::toText() const {
    std::string out = name(global);
    for (const auto& [c, level] : own) out += "; " + c + "=" + name(level);
    return out;
}

JPLogLevels JPLogLevels::fromText(const std::string& text) {
    JPLogLevels l;
    std::istringstream in(text);
    std::string part;
    bool first = true;
    while (std::getline(in, part, ';')) {
        part = trimmed(part);
        if (part.empty()) continue;
        const size_t eq = part.find('=');
        JLogLevel level;
        if (eq == std::string::npos) {
            if (first && levelNamed(part, level)) l.global = level;
        } else if (levelNamed(trimmed(part.substr(eq + 1)), level)) {
            l.own[trimmed(part.substr(0, eq))] = level;
        }
        first = false;
    }
    return l;
}

} // inline namespace jf
