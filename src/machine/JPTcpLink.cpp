// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPTcpLink.h"

#include <cerrno>
#include <chrono>
#include <cstring>

#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

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
    addrinfo hints {};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* found = nullptr;
    if (const int r = ::getaddrinfo(m_host.c_str(), std::to_string(m_port).c_str(), &hints, &found); r != 0) {
        error = m_host + ": " + ::gai_strerror(r);
        return false;
    }
    error = describe() + ": cannot connect";
    for (addrinfo* a = found; a; a = a->ai_next) {
        const int s = ::socket(a->ai_family, a->ai_socktype | SOCK_CLOEXEC, a->ai_protocol);
        if (s < 0) continue;
        // Connected without blocking, so a host that does not answer is given up in time.
        const int flags = ::fcntl(s, F_GETFL, 0);
        ::fcntl(s, F_SETFL, flags | O_NONBLOCK);
        int r = ::connect(s, a->ai_addr, a->ai_addrlen);
        if (r < 0 && errno == EINPROGRESS) {
            pollfd p { s, POLLOUT, 0 };
            r = ::poll(&p, 1, kConnectTimeoutMs);
            if (r == 1) {
                int soError = 0;
                socklen_t len = sizeof soError;
                ::getsockopt(s, SOL_SOCKET, SO_ERROR, &soError, &len);
                r = soError == 0 ? 0 : -1;
                if (soError) errno = soError;
            } else {
                if (r == 0) errno = ETIMEDOUT;
                r = -1;
            }
        }
        if (r == 0) {
            ::fcntl(s, F_SETFL, flags);
            // Each command goes as soon as it is written.
            const int one = 1;
            ::setsockopt(s, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
            m_socket = s;
            break;
        }
        error = describe() + ": " + std::strerror(errno);
        ::close(s);
    }
    ::freeaddrinfo(found);
    if (m_socket < 0) return false;
    error.clear();
    m_partial.clear();
    m_lines.clear();
    return true;
}

void JPTcpLink::close() {
    if (m_socket >= 0) ::close(m_socket);
    m_socket = -1;
}

bool JPTcpLink::write(const std::string& bytes) {
    if (m_socket < 0) return false;
    size_t sent = 0;
    while (sent < bytes.size()) {
        const ssize_t n = ::send(m_socket, bytes.data() + sent, bytes.size() - sent, MSG_NOSIGNAL);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) {
            close();
            return false;
        }
        sent += size_t(n);
    }
    return true;
}

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
        if (m_socket < 0) return std::nullopt;
        const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - clock::now()).count();
        if (left <= 0) return std::nullopt;
        pollfd p { m_socket, POLLIN, 0 };
        const int r = ::poll(&p, 1, int(left));
        if (r < 0 && errno == EINTR) continue;
        if (r <= 0) return std::nullopt;
        char chunk[kReadChunk];
        const ssize_t n = ::recv(m_socket, chunk, sizeof chunk, 0);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) {   // closed by the controller, or failed
            close();
            return std::nullopt;
        }
        m_partial.append(chunk, size_t(n));
        split();
    }
    std::string line = std::move(m_lines.front());
    m_lines.pop_front();
    return line;
}

} // inline namespace jf
