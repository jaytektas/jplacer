// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPSimulatedSource.h"

#include <algorithm>
#include <cmath>
#include <thread>

inline namespace jf {

namespace {

// The test picture's grid pitch in pixels and its two shades.
constexpr int     kGrid  = 40;
constexpr uint8_t kDark  = 40;
constexpr uint8_t kLight = 90;

} // namespace

JPSimulatedSource::JPSimulatedSource(std::string name, int width, int height, double fps,
                                     const JJson& scene, ViewProvider view)
    : m_name(std::move(name)), m_mode{ "SIM", width, height, fps }, m_view(std::move(view)) {
    if (!scene.isObject()) return;
    m_hasScene = true;
    for (size_t i = 0; i < 4; ++i) m_pxPerMm[i] = scene["pxPerMm"][i].number();
    for (const JJson& m : scene["marks"].arr())
        m_marks.push_back({ m["x"].number(), m["y"].number(), m["diameter"].number() });
    m_ground = float(scene["ground"].number());
    m_mark   = float(scene["mark"].number());
    m_noise  = float(scene["noise"].number());
}

void JPSimulatedSource::drawScene(JPFrame& frame) {
    // A mark at P appears at centre + M (V - P), V where the camera looks.
    double vx = 0, vy = 0;
    const bool known = m_view && m_view(vx, vy);
    const double cx = frame.width / 2.0, cy = frame.height / 2.0;
    const double scale = std::sqrt(std::abs(m_pxPerMm[0] * m_pxPerMm[3] - m_pxPerMm[1] * m_pxPerMm[2]));
    std::normal_distribution<float> noise(0, m_noise > 0 ? m_noise : 1e-6f);
    std::vector<float> lum(size_t(frame.width) * size_t(frame.height));
    for (int y = 0; y < frame.height; ++y)
        for (int x = 0; x < frame.width; ++x)   // light falling off from one corner
            lum[size_t(y) * size_t(frame.width) + size_t(x)] =
                m_ground * (1.3f - 0.5f * float(x + y) / float(frame.width + frame.height));
    constexpr int kSub = 3;   // supersampling: edges as soft as a lens makes them
    for (const Mark& m : m_marks) {
        if (!known) break;
        const double dx = vx - m.x, dy = vy - m.y;
        const double px = cx + m_pxPerMm[0] * dx + m_pxPerMm[1] * dy;
        const double py = cy + m_pxPerMm[2] * dx + m_pxPerMm[3] * dy;
        const double r = m.diameter * scale / 2;
        for (int y = int(py - r - 2); y <= int(py + r + 2); ++y)
            for (int x = int(px - r - 2); x <= int(px + r + 2); ++x) {
                if (x < 0 || y < 0 || x >= frame.width || y >= frame.height) continue;
                int inside = 0;
                for (int sy = 0; sy < kSub; ++sy)
                    for (int sx = 0; sx < kSub; ++sx) {
                        const double ox = x + (sx + 0.5) / kSub - 0.5 - px, oy = y + (sy + 0.5) / kSub - 0.5 - py;
                        inside += ox * ox + oy * oy <= r * r;
                    }
                float& v = lum[size_t(y) * size_t(frame.width) + size_t(x)];
                const float a = float(inside) / (kSub * kSub);
                v = v * (1 - a) + m_mark * a;
            }
    }
    uint8_t* p = frame.rgba.data();
    for (float v : lum) {
        const uint8_t g = uint8_t(std::clamp(v + noise(m_rng), 0.f, 255.f));
        *p++ = g; *p++ = g; *p++ = g; *p++ = 255;
    }
}

bool JPSimulatedSource::open(std::string& error) {
    if (m_mode.width <= 0 || m_mode.height <= 0 || m_mode.fps <= 0) {
        error = m_name + ": a simulated camera needs a width, a height and a frame rate";
        return false;
    }
    return true;
}

std::vector<JPCaptureMode> JPSimulatedSource::modes() const { return { m_mode }; }

bool JPSimulatedSource::start(const JPCaptureMode&, std::string&) {
    m_next = std::chrono::steady_clock::now();
    return true;
}

bool JPSimulatedSource::grab(JPFrame& frame, int timeoutMs, std::string&) {
    const auto now = std::chrono::steady_clock::now();
    if (m_next > now + std::chrono::milliseconds(timeoutMs)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(timeoutMs));
        return false;
    }
    std::this_thread::sleep_until(m_next);
    m_next += std::chrono::microseconds(int64_t(1e6 / m_mode.fps));

    frame.width  = m_mode.width;
    frame.height = m_mode.height;
    frame.rgba.resize(size_t(frame.width) * size_t(frame.height) * 4);
    if (m_hasScene) {
        drawScene(frame);
        frame.sequence = ++m_sequence;
        return true;
    }
    const int bar = int(m_sequence * 4 % uint64_t(frame.width));
    uint8_t* p = frame.rgba.data();
    for (int y = 0; y < frame.height; ++y)
        for (int x = 0; x < frame.width; ++x) {
            const bool line = x % kGrid == 0 || y % kGrid == 0;
            const uint8_t v = line ? kLight : kDark;
            *p++ = v;
            *p++ = (x >= bar && x < bar + kGrid / 4) ? kLight : v;
            *p++ = v;
            *p++ = 255;
        }
    frame.sequence = ++m_sequence;
    return true;
}

} // inline namespace jf
