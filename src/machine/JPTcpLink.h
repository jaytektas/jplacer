// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPLink.h"

#include <deque>
#include <string>

inline namespace jf {

// A controller reached over TCP (OpenPnP's TcpCommunications): a host name
// or IP address and a port, 23 (telnet) to begin with, as a grblHAL board on
// Ethernet listens.
class JPTcpLink : public JPLink {
public:
    static constexpr int kDefaultPort = 23;
    // How long a connection may take before it is given up, in ms.
    static constexpr int kConnectTimeoutMs = 5000;

    JPTcpLink(std::string host, int port) : m_host(std::move(host)), m_port(port) {}
    ~JPTcpLink() override { close(); }

    bool open(std::string& error) override;
    void close() override;
    bool isOpen() const override { return m_socket >= 0; }
    bool write(const std::string& bytes) override;
    std::optional<std::string> readLine(int timeoutMs) override;
    std::string describe() const override { return m_host + ":" + std::to_string(m_port); }

private:
    // Move complete lines out of m_partial into m_lines.
    void split();

    std::string             m_host;
    int                     m_port;
    int                     m_socket = -1;
    std::string             m_partial;
    std::deque<std::string> m_lines;
};

} // inline namespace jf
