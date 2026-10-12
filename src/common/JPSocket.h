// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

inline namespace jf {

// A TCP connection, the same on Linux and Windows (BSD sockets, Winsock): to a host and port within a time,
// bytes sent and received, each wait bounded. Owns its socket (closed when it goes); moved, not copied. What a
// TCP controller link and a network camera's stream talk over.
class JPSocket {
public:
    JPSocket() = default;
    ~JPSocket();
    JPSocket(JPSocket&& other) noexcept;
    JPSocket& operator=(JPSocket&& other) noexcept;
    JPSocket(const JPSocket&) = delete;
    JPSocket& operator=(const JPSocket&) = delete;

    // Connected to `host` (a name or an address) at `port`, each of its addresses tried, each given up after
    // `timeoutMs`; false, and why, when none answered. `noDelay`: each send goes at once (TCP_NODELAY).
    bool connect(const std::string& host, int port, int timeoutMs, bool noDelay, std::string& why);
    bool isOpen() const { return m_handle != kNone; }
    void close();

    // All of `bytes` sent; false when the connection is gone (it is closed).
    bool sendAll(const char* bytes, size_t n);
    bool sendAll(const std::string& bytes) { return sendAll(bytes.data(), bytes.size()); }
    // What has come, waiting at most `timeoutMs` for something (0: only what is there now): how many bytes
    // (at most `n`) were put in `into`; 0 when nothing came in time; -1 when the other end closed it or it
    // failed (it is closed).
    int receive(char* into, size_t n, int timeoutMs);

private:
    static constexpr std::intptr_t kNone = -1;
    std::intptr_t m_handle = kNone;   // the system's socket (a file descriptor, or Winsock's SOCKET)
};

} // inline namespace jf
