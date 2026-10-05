// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPMjpgSource.h"

#include "JPPixels.h"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstring>

#include <netdb.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

inline namespace jf {

namespace {
constexpr size_t kReadChunk = 65536;
// A stream that never sends an end is cut back to its last start beyond this much.
constexpr size_t kMostBuffered = 16u << 20;
}

JPMjpgSource::JPMjpgSource(std::string name, std::string url, int width, int height, int timeoutMs)
    : m_name(std::move(name)), m_url(std::move(url)), m_width(width), m_height(height), m_timeoutMs(timeoutMs) {}

bool JPMjpgSource::open(std::string& error) {
    close();
    // http://host[:port][/path]
    const std::string prefix = "http://";
    if (m_url.rfind(prefix, 0) != 0) {
        error = m_name + ": its stream address must start with http:// (" + m_url + ")";
        return false;
    }
    const std::string rest = m_url.substr(prefix.size());
    const size_t slash = rest.find('/');
    const std::string hostPort = rest.substr(0, slash), path = slash == std::string::npos ? "/" : rest.substr(slash);
    const size_t colon = hostPort.rfind(':');
    const std::string host = colon == std::string::npos ? hostPort : hostPort.substr(0, colon);
    const std::string port = colon == std::string::npos ? "80" : hostPort.substr(colon + 1);
    addrinfo hints {};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* found = nullptr;
    if (const int r = ::getaddrinfo(host.c_str(), port.c_str(), &hints, &found); r != 0) {
        error = m_name + ": " + host + ": " + ::gai_strerror(r);
        return false;
    }
    for (addrinfo* a = found; a && m_socket < 0; a = a->ai_next) {
        const int s = ::socket(a->ai_family, a->ai_socktype | SOCK_CLOEXEC, a->ai_protocol);
        if (s < 0) continue;
        if (::connect(s, a->ai_addr, a->ai_addrlen) == 0) m_socket = s;
        else ::close(s);
    }
    ::freeaddrinfo(found);
    if (m_socket < 0) {
        error = m_name + ": cannot connect to " + m_url + " (" + std::strerror(errno) + ")";
        return false;
    }
    const std::string request = "GET " + path + " HTTP/1.0\r\nHost: " + host + "\r\nUser-Agent: jplacer\r\n\r\n";
    if (::send(m_socket, request.data(), request.size(), MSG_NOSIGNAL) != ssize_t(request.size())) {
        error = m_name + ": the stream could not be asked for";
        close();
        return false;
    }
    m_buffer.clear();
    return true;
}

void JPMjpgSource::close() {
    if (m_socket >= 0) ::close(m_socket);
    m_socket = -1;
}

std::vector<JPCaptureMode> JPMjpgSource::modes() const {
    JPCaptureMode m;
    m.format = "MJPG";
    m.width = m_width;
    m.height = m_height;
    return { m };
}

bool JPMjpgSource::start(const JPCaptureMode&, std::string&) { return true; }

bool JPMjpgSource::takeJpeg(std::vector<uint8_t>& buffer, std::vector<uint8_t>& jpeg) {
    // Each picture from its start (FF D8) to its end (FF D9), forward; the newest complete one kept.
    auto find = [&buffer](size_t from, uint8_t second) {
        for (size_t i = from; i + 1 < buffer.size(); ++i)
            if (buffer[i] == 0xFF && buffer[i + 1] == second) return i;
        return std::string::npos;
    };
    bool got = false;
    size_t consumed = 0;
    for (size_t start = find(0, 0xD8); start != std::string::npos; start = find(consumed, 0xD8)) {
        const size_t end = find(start + 2, 0xD9);
        if (end == std::string::npos) {
            consumed = start;   // the picture still coming kept
            break;
        }
        jpeg.assign(buffer.begin() + long(start), buffer.begin() + long(end + 2));
        got = true;
        consumed = end + 2;
    }
    buffer.erase(buffer.begin(), buffer.begin() + long(std::min(consumed, buffer.size())));
    return got;
}

bool JPMjpgSource::grab(JPFrame& frame, int timeoutMs, std::string& error) {
    if (m_socket < 0) {
        error = m_name + ": not connected";
        return false;
    }
    using clock = std::chrono::steady_clock;
    const auto deadline = clock::now() + std::chrono::milliseconds(std::min(timeoutMs, m_timeoutMs));
    std::vector<uint8_t> jpeg;
    for (;;) {
        // All that has come, then the newest picture in it.
        for (;;) {
            pollfd p { m_socket, POLLIN, 0 };
            if (::poll(&p, 1, 0) <= 0) break;
            uint8_t chunk[kReadChunk];
            const ssize_t n = ::recv(m_socket, chunk, sizeof chunk, 0);
            if (n < 0 && errno == EINTR) continue;
            if (n <= 0) {
                error = m_name + ": the stream ended";
                close();
                return false;
            }
            m_buffer.insert(m_buffer.end(), chunk, chunk + n);
        }
        if (takeJpeg(m_buffer, jpeg)) break;
        if (m_buffer.size() > kMostBuffered) m_buffer.clear();
        const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - clock::now()).count();
        if (left <= 0) return false;
        pollfd p { m_socket, POLLIN, 0 };
        ::poll(&p, 1, int(left));
    }
    if (!JPPixels::jpegToRgba(jpeg.data(), jpeg.size(), frame.width, frame.height, frame.rgba, error)) {
        error = m_name + ": " + error;
        return false;
    }
    frame.captured = clock::now();
    return true;
}

} // inline namespace jf
