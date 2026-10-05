// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPImageSource.h"

#include "JPImageFile.h"

#include <cmath>
#include <thread>

inline namespace jf {

namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr size_t kRgba = 4;
}

JPImageSource::JPImageSource(std::string name, Settings settings, ViewProvider view)
    : m_name(std::move(name)), m_settings(std::move(settings)), m_view(std::move(view)) {}

bool JPImageSource::open(std::string& error) {
    if (m_settings.path.empty()) {
        error = m_name + ": no picture to show (its source file)";
        return false;
    }
    if (m_settings.unitsPerPixelX == 0 || m_settings.unitsPerPixelY == 0) {
        error = m_name + ": its picture's units per pixel are not set";
        return false;
    }
    if (!JPImageFile::readPng(m_settings.path, m_image, error)) {
        error = m_name + ": " + error;
        return false;
    }
    return true;
}

std::vector<JPCaptureMode> JPImageSource::modes() const {
    JPCaptureMode m;
    m.width = m_settings.width;
    m.height = m_settings.height;
    m.fps = m_settings.fps;
    m.format = "RGBA";
    return { m };
}

bool JPImageSource::start(const JPCaptureMode&, std::string&) {
    m_next = std::chrono::steady_clock::now();
    return true;
}

void JPImageSource::render(double x, double y, JPFrame& frame) const {
    const Settings& s = m_settings;
    frame.width = s.width;
    frame.height = s.height;
    frame.rgba.assign(size_t(s.width) * size_t(s.height) * kRgba, 0);
    // Where the view's middle is in the picture: its rows run down, the machine's Y up.
    const double px = (x + s.offsetX) / s.unitsPerPixelX;
    const double py = double(m_image.height) - (y + s.offsetY) / s.unitsPerPixelY;
    const double cx = s.width / 2.0, cy = s.height / 2.0;
    const double r = s.rotation * kPi / 180, cr = std::cos(r), sr = std::sin(r);
    const double sx = s.flipped ? -s.scale : s.scale, sy = s.scale;
    for (int v = 0; v < s.height; ++v)
        for (int u = 0; u < s.width; ++u) {
            // Back through the view's turn and scale, about its middle (OpenPnP's simulated transform undone).
            const double ox = (u - cx) / sx, oy = (v - cy) / sy;
            const double qx = cr * ox - sr * oy, qy = sr * ox + cr * oy;
            const int ix = int(std::floor(px + qx)), iy = int(std::floor(py + qy));
            if (ix < 0 || iy < 0 || ix >= m_image.width || iy >= m_image.height) continue;
            const size_t from = (size_t(iy) * size_t(m_image.width) + size_t(ix)) * kRgba;
            const size_t to = (size_t(v) * size_t(s.width) + size_t(u)) * kRgba;
            for (size_t k = 0; k < kRgba; ++k) frame.rgba[to + k] = m_image.rgba[from + k];
        }
}

bool JPImageSource::grab(JPFrame& frame, int timeoutMs, std::string&) {
    // At the camera's rate.
    const auto now = std::chrono::steady_clock::now();
    if (m_next > now) {
        if (m_next - now > std::chrono::milliseconds(timeoutMs)) return false;
        std::this_thread::sleep_until(m_next);
    }
    m_next = std::max(m_next, now) + std::chrono::microseconds(int64_t(1e6 / std::max(1.0, m_settings.fps)));
    double x = 0, y = 0;
    if (m_view && !m_view(x, y)) x = y = 0;
    render(x, y, frame);
    frame.captured = std::chrono::steady_clock::now();
    return true;
}

} // inline namespace jf
