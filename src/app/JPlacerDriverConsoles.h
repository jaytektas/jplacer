// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include <deque>
#include <map>
#include <mutex>
#include <string>
#include <vector>

inline namespace jf {

// What passes between jplacer and each controller, kept for its Machine Setup
// Console tab (OpenPnP's driver Console): the newest kMostKept lines each,
// by the controller's name. Lines come in on the controllers' threads.
class JPlacerDriverConsoles {
public:
    static constexpr size_t kMostKept = 200;

    void add(const std::string& controller, bool sent, const std::string& line);
    std::vector<std::string> lines(const std::string& controller) const;

private:
    mutable std::mutex                             m_mutex;
    std::map<std::string, std::deque<std::string>> m_lines;
};

} // inline namespace jf
