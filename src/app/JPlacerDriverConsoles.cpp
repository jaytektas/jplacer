// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPlacerDriverConsoles.h"

inline namespace jf {

void JPlacerDriverConsoles::add(const std::string& controller, bool sent, const std::string& line) {
    std::lock_guard lk(m_mutex);
    std::deque<std::string>& kept = m_lines[controller];
    // As the Console panel shows them: sent, an arrow out; received, an arrow in.
    kept.push_back((sent ? "\xE2\x86\x92 " : "\xE2\x86\x90 ") + line);
    while (kept.size() > kMostKept) kept.pop_front();
}

std::vector<std::string> JPlacerDriverConsoles::lines(const std::string& controller) const {
    std::lock_guard lk(m_mutex);
    const auto it = m_lines.find(controller);
    return it == m_lines.end() ? std::vector<std::string> {} : std::vector<std::string>(it->second.begin(), it->second.end());
}

} // inline namespace jf
