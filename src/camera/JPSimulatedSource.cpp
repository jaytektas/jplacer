// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPSimulatedSource.h"

#include "common/JPLens.h"

#include <algorithm>
#include <cmath>
#include <thread>

inline namespace jf {

namespace {

// The test picture's grid pitch in pixels and its two shades.
constexpr int     kGrid  = 40;
constexpr uint8_t kDark  = 40;
// OpenPnP's: a light off shades the picture by 200 of 255; a spark is white at up to 15 of 255.
constexpr float kDarkShare = 55.f / 255.f;
constexpr int   kSparkAlphaMost = 15;
constexpr uint8_t kLight = 90;

} // namespace

JPSimulatedSource::JPSimulatedSource(std::string name, int width, int height, double fps,
                                     const JJson& scene, ViewProvider view, int hangAfterFrames,
                                     int freezeAfterFrames, ExtrasProvider extras)
    : m_name(std::move(name)), m_mode{ "SIM", width, height, fps }, m_view(std::move(view)), m_extras(std::move(extras)),
      m_hangAfterFrames(hangAfterFrames), m_freezeAfterFrames(freezeAfterFrames) {
    if (!scene.isObject()) return;
    m_hasScene = true;
    for (size_t i = 0; i < 4; ++i) m_pxPerMm[i] = scene["pxPerMm"][i].number();
    auto grey = [](double level) { return Rgb { float(level), float(level), float(level) }; };
    for (const JJson& m : scene["marks"].arr())
        m_marks.push_back({ m["x"].number(), m["y"].number(), m["diameter"].number(),
                            grey(m["level"].number(scene["mark"].number())) });
    for (const JJson& sh : scene["shapes"].arr()) {
        Shape shape { {}, grey(sh["level"].number(scene["mark"].number())) };
        for (const JJson& pt : sh["points"].arr()) shape.points.push_back({ pt[size_t(0)].number(), pt[size_t(1)].number() });
        if (shape.points.size() >= 3) m_shapes.push_back(std::move(shape));
    }
    m_ground = grey(scene["ground"].number());
    if (scene["groundColor"].size() == 3)
        for (size_t k = 0; k < 3; ++k) m_ground[k] = float(scene["groundColor"][k].number());
    m_noise  = float(scene["noise"].number());
    m_lensK1 = scene["lensK1"].number();
    m_lensK2 = scene["lensK2"].number();
    m_lensCentre[0] = scene["lensCentre"][0].number();
    m_lensCentre[1] = scene["lensCentre"][1].number();
    m_lensCentreSet = scene["lensCentre"].size() == 2;
}

void JPSimulatedSource::drawScene(JPFrame& frame) {
    // A mark at P appears at centre + M (V - P) through a perfect lens, V
    // where the camera looks; then the lens bends it.
    double vx = 0, vy = 0;
    const bool known = m_view && m_view(vx, vy);
    const int width = frame.width, height = frame.height;
    const double cx = width / 2.0, cy = height / 2.0;
    const double scale = std::sqrt(std::abs(m_pxPerMm[0] * m_pxPerMm[3] - m_pxPerMm[1] * m_pxPerMm[2]));
    std::normal_distribution<float> noise(0, m_noise > 0 ? m_noise : 1e-6f);
    // Red, green and blue, each pixel; light falling off from one corner.
    std::vector<float> rgb(size_t(width) * size_t(height) * 3);
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x) {
            const float fall = 1.3f - 0.5f * float(x + y) / float(width + height);
            for (size_t k = 0; k < 3; ++k) rgb[(size_t(y) * size_t(width) + size_t(x)) * 3 + k] = m_ground[k] * fall;
        }
    constexpr int kSub = 3;   // supersampling: edges as soft as a lens makes them
    // The lens bends the picture: each point is drawn where the lens puts it,
    // so a pixel is tested by straightening it first.
    const JPLens lens = m_lensCentreSet
        ? JPLens::forPicture(width, height, m_lensK1, m_lensCentre[0], m_lensCentre[1], m_lensK2)
        : JPLens::forPicture(width, height, m_lensK1, width / 2.0, height / 2.0, m_lensK2);
    // What is at the viewpoint is seen in the middle of the picture: through
    // a perfect lens, at the middle straightened.
    double ox0, oy0;
    lens.undistort(cx, cy, ox0, oy0);
    // Laid over the picture: how much of each pixel of a box it covers, out of focus by `blurPx` (OpenPnP's
    // focal blur, a disc kernel), in its colour.
    auto lay = [&](int left, int top, int right, int bottom, const std::vector<float>& cover, const Rgb& color, float blurPx) {
        const int bw = right - left + 1, bh = bottom - top + 1;
        std::vector<float> c = cover;
        if (blurPx > 0.01f) {
            const int size = int(std::ceil(blurPx)) * 2 + 1, half = size / 2;
            std::vector<float> kernel(size_t(size) * size_t(size));
            float sum = 0;
            int num = 0;
            for (size_t i = 0; i < kernel.size(); ++i) {
                const double kx = double(int(i) / size) - size / 2.0 + 0.5, ky = double(int(i) % size) - size / 2.0 + 0.5;
                kernel[i] = float(std::clamp(blurPx + 1 - std::hypot(kx, ky), 0.0, 1.0));
                sum += kernel[i];
                if (kernel[i] > 0) ++num;
            }
            if (num > 1) {
                for (float& k : kernel) k /= sum;
                for (int v = 0; v < bh; ++v)
                    for (int u = 0; u < bw; ++u) {
                        float acc = 0;
                        for (int i = 0; i < size; ++i)
                            for (int j = 0; j < size; ++j) {
                                const int su = u + i - half, sv = v + j - half;
                                if (su < 0 || sv < 0 || su >= bw || sv >= bh) continue;
                                acc += kernel[size_t(i) * size_t(size) + size_t(j)] * cover[size_t(sv) * size_t(bw) + size_t(su)];
                            }
                        c[size_t(v) * size_t(bw) + size_t(u)] = acc;
                    }
            }
        }
        for (int v = 0; v < bh; ++v)
            for (int u = 0; u < bw; ++u) {
                const float a = c[size_t(v) * size_t(bw) + size_t(u)];
                if (a <= 0) continue;
                float* p = &rgb[(size_t(v + top) * size_t(width) + size_t(u + left)) * 3];
                for (size_t k = 0; k < 3; ++k) p[k] = p[k] * (1 - a) + color[k] * a;
            }
    };
    // The box round a thing seen from (x0, y0) to (x1, y1), room left for its blur; false when off the picture.
    auto box = [&](double x0, double y0, double x1, double y1, float blurPx, int& left, int& top, int& right, int& bottom) {
        const int pad = 2 + int(std::ceil(blurPx));
        left = std::max(0, int(std::floor(x0)) - pad);
        top = std::max(0, int(std::floor(y0)) - pad);
        right = std::min(width - 1, int(std::ceil(x1)) + pad);
        bottom = std::min(height - 1, int(std::ceil(y1)) + pad);
        return left <= right && top <= bottom;
    };
    // The scene's marks, and what the machine adds over it (nozzle tips).
    const Extras extras = m_extras ? m_extras() : Extras {};
    std::vector<Mark> marks = m_marks;
    auto colorOf = [](float level, const std::optional<Rgb>& color) { return color ? *color : Rgb { level, level, level }; };
    for (const Extras::Spot& s : extras.spots) marks.push_back({ s.x, s.y, s.diameter, colorOf(s.level, s.color), s.blurPx });
    for (const Mark& m : marks) {
        if (!known) break;
        const double dx = vx - m.x, dy = vy - m.y;
        const double px = ox0 + m_pxPerMm[0] * dx + m_pxPerMm[1] * dy;
        const double py = oy0 + m_pxPerMm[2] * dx + m_pxPerMm[3] * dy;
        const double r = m.diameter * scale / 2;
        double sx0, sy0;   // where the lens shows the mark's middle
        lens.distort(px, py, sx0, sy0);
        const double reach = r * (1 + std::abs(m_lensK1)) + 2;
        int left, top, right, bottom;
        if (!box(sx0 - reach, sy0 - reach, sx0 + reach, sy0 + reach, m.blurPx, left, top, right, bottom)) continue;
        std::vector<float> cover(size_t(right - left + 1) * size_t(bottom - top + 1), 0.f);
        for (int y = top; y <= bottom; ++y)
            for (int x = left; x <= right; ++x) {
                int inside = 0;
                for (int sy = 0; sy < kSub; ++sy)
                    for (int sx = 0; sx < kSub; ++sx) {
                        double ux, uy;
                        lens.undistort(x + (sx + 0.5) / kSub - 0.5, y + (sy + 0.5) / kSub - 0.5, ux, uy);
                        const double ox = ux - px, oy = uy - py;
                        inside += ox * ox + oy * oy <= r * r;
                    }
                cover[size_t(y - top) * size_t(right - left + 1) + size_t(x - left)] = float(inside) / (kSub * kSub);
            }
        lay(left, top, right, bottom, cover, m.color, m.blurPx);
    }
    // The shapes: each pixel straightened and taken back to the machine, and
    // lit where it falls inside one.
    const double det = m_pxPerMm[0] * m_pxPerMm[3] - m_pxPerMm[1] * m_pxPerMm[2];
    std::vector<Shape> shapes = m_shapes;
    for (const Extras::Outline& o : extras.outlines)
        if (o.points.size() >= 3) shapes.push_back({ o.points, colorOf(o.level, o.color), o.blurPx });
    for (const Shape& sh : shapes) {
        if (!known || std::abs(det) < 1e-12) break;
        // Where its corners are seen, for the pixels to test.
        double x0 = 1e300, y0 = 1e300, x1 = -1e300, y1 = -1e300;
        for (const auto& [mx, my] : sh.points) {
            const double dx = vx - mx, dy = vy - my;
            double sx, sy;
            lens.distort(ox0 + m_pxPerMm[0] * dx + m_pxPerMm[1] * dy, oy0 + m_pxPerMm[2] * dx + m_pxPerMm[3] * dy, sx, sy);
            x0 = std::min(x0, sx); x1 = std::max(x1, sx);
            y0 = std::min(y0, sy); y1 = std::max(y1, sy);
        }
        auto inside = [&sh](double mx, double my) {
            int sign = 0;
            for (size_t i = 0; i < sh.points.size(); ++i) {
                const auto& [ax, ay] = sh.points[i];
                const auto& [bx, by] = sh.points[(i + 1) % sh.points.size()];
                const double cross = (bx - ax) * (my - ay) - (by - ay) * (mx - ax);
                const int s = cross > 0 ? 1 : cross < 0 ? -1 : 0;
                if (s != 0 && sign != 0 && s != sign) return false;
                if (s != 0) sign = s;
            }
            return true;
        };
        int left, top, right, bottom;
        if (!box(x0, y0, x1, y1, sh.blurPx, left, top, right, bottom)) continue;
        std::vector<float> cover(size_t(right - left + 1) * size_t(bottom - top + 1), 0.f);
        for (int y = top; y <= bottom; ++y)
            for (int x = left; x <= right; ++x) {
                int in = 0;
                for (int sy = 0; sy < kSub; ++sy)
                    for (int sx = 0; sx < kSub; ++sx) {
                        double ux, uy;
                        lens.undistort(x + (sx + 0.5) / kSub - 0.5, y + (sy + 0.5) / kSub - 0.5, ux, uy);
                        // u = o + M (V - P): P = V - M^-1 (u - o).
                        const double ex = ux - ox0, ey = uy - oy0;
                        const double dx = (m_pxPerMm[3] * ex - m_pxPerMm[1] * ey) / det;
                        const double dy = (-m_pxPerMm[2] * ex + m_pxPerMm[0] * ey) / det;
                        in += inside(vx - dx, vy - dy);
                    }
                cover[size_t(y - top) * size_t(right - left + 1) + size_t(x - left)] = float(in) / (kSub * kSub);
            }
        lay(left, top, right, bottom, cover, sh.color, sh.blurPx);
    }
    // OpenPnP's simulated exposure: shaded while the light is off, and sparks
    // of noise (faint short lines, so no two pictures are alike).
    if (extras.dark)
        for (float& v : rgb) v *= kDarkShare;
    if (extras.sparks > 0) {
        std::uniform_int_distribution<int> count(0, extras.sparks - 1), dir(-1, 1), alpha(0, kSparkAlphaMost);
        std::uniform_int_distribution<int> px(0, width - 1), py(0, height - 1);
        for (int n = count(m_rng); n > 0; --n) {
            const int x = px(m_rng), y = py(m_rng), x2 = x + dir(m_rng), y2 = y + dir(m_rng);
            const float a = float(alpha(m_rng)) / 255.f;
            for (const auto& [sx, sy] : { std::pair { x, y }, std::pair { x2, y2 } })
                if (sx >= 0 && sy >= 0 && sx < width && sy < height) {
                    float* p = &rgb[(size_t(sy) * size_t(width) + size_t(sx)) * 3];
                    for (size_t k = 0; k < 3; ++k) p[k] = p[k] * (1 - a) + 255.f * a;
                }
        }
    }
    // One noise for a pixel's three: a grey picture stays grey.
    uint8_t* p = frame.rgba.data();
    for (size_t i = 0; i < rgb.size(); i += 3) {
        const float n = noise(m_rng);
        for (size_t k = 0; k < 3; ++k) *p++ = uint8_t(std::clamp(rgb[i + k] + n, 0.f, 255.f));
        *p++ = 255;
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
    if (m_hangAfterFrames > 0 && m_sequence >= uint64_t(m_hangAfterFrames)) {   // wedged: nothing, no error
        std::this_thread::sleep_for(std::chrono::milliseconds(timeoutMs));
        return false;
    }
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
        frame.captured = std::chrono::steady_clock::now();   // the moment the view is read
        drawScene(frame);
        frame.sequence = ++m_sequence;
        return true;
    }
    // Frozen: the bar stays where it was, the picture the same every time.
    const uint64_t shown = m_freezeAfterFrames > 0 ? std::min<uint64_t>(m_sequence, uint64_t(m_freezeAfterFrames)) : m_sequence;
    const int bar = int(shown * 4 % uint64_t(frame.width));
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
    frame.captured = std::chrono::steady_clock::now();
    return true;
}

} // inline namespace jf
