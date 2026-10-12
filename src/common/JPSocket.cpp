// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPSocket.h"

#include <cerrno>
#include <cstring>
#include <utility>

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#include <mutex>
#else
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

inline namespace jf {

namespace {

#if defined(_WIN32)
using Native = SOCKET;
const Native kInvalid = INVALID_SOCKET;
// Winsock started once, for the whole run.
void startNetworking() {
    static std::once_flag once;
    std::call_once(once, [] {
        WSADATA data;
        WSAStartup(MAKEWORD(2, 2), &data);
    });
}
std::string lastError() {
    char text[256] = {};
    FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, DWORD(WSAGetLastError()), 0,
                   text, sizeof text, nullptr);
    std::string s = text;
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == '.')) s.pop_back();
    return s;
}
void closeNative(Native s) { ::closesocket(s); }
// Blocking, or not (so a connect can be given up in time).
void setBlocking(Native s, bool on) {
    u_long nonBlocking = on ? 0 : 1;
    ::ioctlsocket(s, FIONBIO, &nonBlocking);
}
bool connectPending() { return WSAGetLastError() == WSAEWOULDBLOCK; }
// Waits for `s` to be readable (or writable); 1 ready, 0 timed out, <0 failed.
int waitFor(Native s, bool write, int timeoutMs) {
    WSAPOLLFD p {};
    p.fd = s;
    p.events = write ? POLLWRNORM : POLLRDNORM;
    return ::WSAPoll(&p, 1, timeoutMs);
}
bool interrupted() { return false; }
constexpr int kSendFlags = 0;
#else
using Native = int;
const Native kInvalid = -1;
void startNetworking() {}
std::string lastError() { return std::strerror(errno); }
void closeNative(Native s) { ::close(s); }
void setBlocking(Native s, bool on) {
    const int flags = ::fcntl(s, F_GETFL, 0);
    ::fcntl(s, F_SETFL, on ? (flags & ~O_NONBLOCK) : (flags | O_NONBLOCK));
}
bool connectPending() { return errno == EINPROGRESS; }
int waitFor(Native s, bool write, int timeoutMs) {
    pollfd p { s, short(write ? POLLOUT : POLLIN), 0 };
    return ::poll(&p, 1, timeoutMs);
}
bool interrupted() { return errno == EINTR; }
constexpr int kSendFlags = MSG_NOSIGNAL;   // a peer gone is an error returned, not a signal
#endif

Native native(std::intptr_t h) { return Native(h); }

} // namespace

JPSocket::~JPSocket() { close(); }

JPSocket::JPSocket(JPSocket&& other) noexcept : m_handle(std::exchange(other.m_handle, kNone)) {}

JPSocket& JPSocket::operator=(JPSocket&& other) noexcept {
    if (this != &other) {
        close();
        m_handle = std::exchange(other.m_handle, kNone);
    }
    return *this;
}

void JPSocket::close() {
    if (m_handle != kNone) closeNative(native(m_handle));
    m_handle = kNone;
}

bool JPSocket::connect(const std::string& host, int port, int timeoutMs, bool noDelay, std::string& why) {
    close();
    startNetworking();
    addrinfo hints {};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* found = nullptr;
    if (const int r = ::getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &found); r != 0) {
        why = host + ": " + ::gai_strerror(r);
        return false;
    }
    why = host + ":" + std::to_string(port) + ": cannot connect";
    for (addrinfo* a = found; a; a = a->ai_next) {
        const Native s = ::socket(a->ai_family, a->ai_socktype, a->ai_protocol);
        if (s == kInvalid) continue;
#if !defined(_WIN32)
        ::fcntl(s, F_SETFD, FD_CLOEXEC);   // not handed to a process started later
#endif
        // Connected without blocking, so a host that does not answer is given up in time.
        setBlocking(s, false);
        int r = ::connect(s, a->ai_addr, int(a->ai_addrlen));
        if (r != 0 && connectPending()) {
            r = waitFor(s, true, timeoutMs);
            if (r == 1) {
                int soError = 0;
                socklen_t len = sizeof soError;
                ::getsockopt(s, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&soError), &len);
                r = soError == 0 ? 0 : -1;
                if (soError) why = host + ":" + std::to_string(port) + ": " + std::strerror(soError);
            } else {
                if (r == 0) why = host + ":" + std::to_string(port) + ": no answer in " + std::to_string(timeoutMs) + " ms";
                r = -1;
            }
        } else if (r != 0) {
            why = host + ":" + std::to_string(port) + ": " + lastError();
        }
        if (r == 0) {
            setBlocking(s, true);
            if (noDelay) {
                const int one = 1;
                ::setsockopt(s, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&one), sizeof one);
            }
            m_handle = std::intptr_t(s);
            break;
        }
        closeNative(s);
    }
    ::freeaddrinfo(found);
    if (m_handle == kNone) return false;
    why.clear();
    return true;
}

bool JPSocket::sendAll(const char* bytes, size_t n) {
    size_t sent = 0;
    while (sent < n) {
        if (m_handle == kNone) return false;
        const auto r = ::send(native(m_handle), bytes + sent, int(n - sent), kSendFlags);
        if (r < 0 && interrupted()) continue;
        if (r <= 0) {
            close();
            return false;
        }
        sent += size_t(r);
    }
    return true;
}

int JPSocket::receive(char* into, size_t n, int timeoutMs) {
    for (;;) {
        if (m_handle == kNone) return -1;
        const int ready = waitFor(native(m_handle), false, timeoutMs);
        if (ready < 0 && interrupted()) continue;
        if (ready < 0) {
            close();
            return -1;
        }
        if (ready == 0) return 0;
        const auto r = ::recv(native(m_handle), into, int(n), 0);
        if (r < 0 && interrupted()) continue;
        if (r <= 0) {   // closed by the other end, or failed
            close();
            return -1;
        }
        return int(r);
    }
}

} // inline namespace jf
