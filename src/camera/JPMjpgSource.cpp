// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPMjpgSource.h"

#include "JPPixels.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>

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
    const int port = colon == std::string::npos ? 80 : std::atoi(hostPort.substr(colon + 1).c_str());
    // Given up after its timeout, as a picture is.
    std::string why;
    if (!m_socket.connect(host, port, m_timeoutMs, false, why)) {
        error = m_name + ": cannot connect to " + m_url + " (" + why + ")";
        return false;
    }
    const std::string request = "GET " + path + " HTTP/1.0\r\nHost: " + host + "\r\nUser-Agent: jplacer\r\n\r\n";
    if (!m_socket.sendAll(request)) {
        error = m_name + ": the stream could not be asked for";
        close();
        return false;
    }
    m_buffer.clear();
    return true;
}

void JPMjpgSource::close() { m_socket.close(); }

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
    if (!m_socket.isOpen()) {
        error = m_name + ": not connected";
        return false;
    }
    using clock = std::chrono::steady_clock;
    const auto deadline = clock::now() + std::chrono::milliseconds(std::min(timeoutMs, m_timeoutMs));
    std::vector<uint8_t> jpeg;
    for (;;) {
        // All that has come, then the newest picture in it.
        for (;;) {
            char chunk[kReadChunk];
            const int n = m_socket.receive(chunk, sizeof chunk, 0);
            if (n == 0) break;
            if (n < 0) {
                error = m_name + ": the stream ended";
                close();
                return false;
            }
            m_buffer.insert(m_buffer.end(), reinterpret_cast<uint8_t*>(chunk), reinterpret_cast<uint8_t*>(chunk) + n);
        }
        if (takeJpeg(m_buffer, jpeg)) break;
        if (m_buffer.size() > kMostBuffered) m_buffer.clear();
        const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - clock::now()).count();
        if (left <= 0) return false;
        // Waited for more (what came is read on the next round).
        char chunk[kReadChunk];
        const int n = m_socket.receive(chunk, sizeof chunk, int(left));
        if (n < 0) {
            error = m_name + ": the stream ended";
            close();
            return false;
        }
        m_buffer.insert(m_buffer.end(), reinterpret_cast<uint8_t*>(chunk), reinterpret_cast<uint8_t*>(chunk) + n);
    }
    if (!JPPixels::jpegToRgba(jpeg.data(), jpeg.size(), frame.width, frame.height, frame.rgba, error)) {
        error = m_name + ": " + error;
        return false;
    }
    frame.captured = clock::now();
    return true;
}

} // inline namespace jf
