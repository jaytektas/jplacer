// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPTcpLink.h"

#include <chrono>

inline namespace jf {

namespace {
constexpr size_t kReadChunk = 4096;
}

bool JPTcpLink::open(std::string& error) {
    close();
    if (m_host.empty() || m_port <= 0) {
        error = "a TCP link needs an IP address (or host name) and a port";
        return false;
    }
    // Each command goes as soon as it is written (no delay).
    if (!m_socket.connect(m_host, m_port, kConnectTimeoutMs, true, error)) return false;
    m_partial.clear();
    m_lines.clear();
    return true;
}

void JPTcpLink::close() { m_socket.close(); }

bool JPTcpLink::write(const std::string& bytes) { return m_socket.sendAll(bytes); }

void JPTcpLink::split() {
    size_t eol;
    while ((eol = m_partial.find_first_of("\r\n")) != std::string::npos) {
        std::string line = m_partial.substr(0, eol);
        m_partial.erase(0, eol + 1);
        if (!line.empty()) m_lines.push_back(std::move(line));
    }
}

std::optional<std::string> JPTcpLink::readLine(int timeoutMs) {
    using clock = std::chrono::steady_clock;
    const auto deadline = clock::now() + std::chrono::milliseconds(timeoutMs);
    while (m_lines.empty()) {
        if (!m_socket.isOpen()) return std::nullopt;
        const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - clock::now()).count();
        if (left <= 0) return std::nullopt;
        char chunk[kReadChunk];
        const int n = m_socket.receive(chunk, sizeof chunk, int(left));
        if (n <= 0) return std::nullopt;   // nothing in time, or closed by the controller
        m_partial.append(chunk, size_t(n));
        split();
    }
    std::string line = std::move(m_lines.front());
    m_lines.pop_front();
    return line;
}

} // inline namespace jf
