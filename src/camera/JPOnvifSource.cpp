// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPOnvifSource.h"

#include "JPPixels.h"

#include <j/io/HttpClient.h>

#include <algorithm>
#include <cmath>
#include <thread>

inline namespace jf {

JPOnvifSource::JPOnvifSource(std::string name, Settings settings)
    : m_name(std::move(name)), m_settings(std::move(settings)),
      m_onvif(m_settings.host, m_settings.username, m_settings.password) {}

bool JPOnvifSource::open(std::string& error) {
    if (!m_onvif.open(m_settings.preferredResolution, error)) {
        error = m_name + ": " + error;
        return false;
    }
    return true;
}

std::vector<JPCaptureMode> JPOnvifSource::modes() const {
    JPCaptureMode m;
    m.format = "JPEG";
    m.width = m_settings.resizeWidth > 0 ? m_settings.resizeWidth : m_onvif.chosen().width;
    m.height = m_settings.resizeHeight > 0 ? m_settings.resizeHeight : m_onvif.chosen().height;
    m.fps = m_settings.fps;
    return { m };
}

bool JPOnvifSource::grab(JPFrame& frame, int timeoutMs, std::string& error) {
    using clock = std::chrono::steady_clock;
    // No oftener than its rate.
    if (m_settings.fps > 0) {
        const auto next = m_last + std::chrono::duration_cast<clock::duration>(std::chrono::duration<double>(1.0 / m_settings.fps));
        if (clock::now() < next) std::this_thread::sleep_until(next);
    }
    m_last = clock::now();
    std::vector<JHttpHeader> headers;
    if (!m_settings.username.empty())
        headers.push_back({ "Authorization", "Basic " + JPOnvif::base64(m_settings.username + ":" + m_settings.password) });
    const JHttpResponse r = JHttpClient::getSync(m_onvif.snapshotUri(), timeoutMs, headers);
    if (!r.ok()) {
        error = m_name + ": its snapshot: " + (r.error.empty() ? "HTTP " + std::to_string(r.status) : r.error);
        return false;
    }
    int width = 0, height = 0;
    std::vector<uint8_t> rgba;
    if (!JPPixels::jpegToRgba(r.body.data(), r.body.size(), width, height, rgba, error)) {
        error = m_name + ": " + error;
        return false;
    }
    const int w = m_settings.resizeWidth > 0 ? m_settings.resizeWidth : width;
    const int h = m_settings.resizeHeight > 0 ? m_settings.resizeHeight : height;
    if (w != width || h != height) resize(rgba, width, height, w, h, frame.rgba);
    else frame.rgba = std::move(rgba);
    frame.width = w;
    frame.height = h;
    frame.captured = m_last;
    return true;
}

void JPOnvifSource::resize(const std::vector<uint8_t>& rgba, int width, int height, int w, int h, std::vector<uint8_t>& out) {
    out.assign(size_t(w) * size_t(h) * 4, 0);
    const double sx = double(width) / w, sy = double(height) / h;
    for (int y = 0; y < h; ++y) {
        // The source rows and columns this pixel covers (at least one).
        const int y0 = std::min(height - 1, int(std::floor(y * sy)));
        const int y1 = std::max(y0 + 1, std::min(height, int(std::ceil((y + 1) * sy))));
        for (int x = 0; x < w; ++x) {
            const int x0 = std::min(width - 1, int(std::floor(x * sx)));
            const int x1 = std::max(x0 + 1, std::min(width, int(std::ceil((x + 1) * sx))));
            unsigned sum[4] = { 0, 0, 0, 0 };
            for (int yy = y0; yy < y1; ++yy)
                for (int xx = x0; xx < x1; ++xx)
                    for (int c = 0; c < 4; ++c) sum[c] += rgba[(size_t(yy) * size_t(width) + size_t(xx)) * 4 + size_t(c)];
            const unsigned n = unsigned((y1 - y0) * (x1 - x0));
            for (int c = 0; c < 4; ++c) out[(size_t(y) * size_t(w) + size_t(x)) * 4 + size_t(c)] = uint8_t((sum[c] + n / 2) / n);
        }
    }
}

std::string JPOnvifSource::describe() const {
    return m_name + " (ONVIF " + m_settings.host + (m_onvif.describe().empty() ? "" : ", " + m_onvif.describe()) + ")";
}

} // inline namespace jf
