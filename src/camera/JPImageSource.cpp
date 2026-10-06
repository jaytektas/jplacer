// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPImageSource.h"

#include "JPImageFile.h"

#include <algorithm>
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
    // OpenPnP's Simulated Calibration Rig: the camera as far above the primary fiducial as its lens and
    // sensor say for the view it has; the secondary nearer or further, so bigger or smaller, and blurred.
    const double cameraViewDiagonal = std::hypot(s.unitsPerPixelX * s.width, s.unitsPerPixelY * s.height);
    const double cameraDistance = s.focalLengthMm * cameraViewDiagonal / s.sensorDiagonalMm;
    const double primaryZ = s.primaryFiducial ? s.primaryFiducial->z : 0;
    if (s.primaryFiducial)
        drawFiducial(frame, s.primaryFiducial->x - x, s.primaryFiducial->y - y, s.unitsPerPixelX, s.unitsPerPixelY, false);
    if (s.secondaryFiducial) {
        const double secondaryDistance = cameraDistance + primaryZ - s.secondaryFiducial->z;
        const double tilted = std::tan(s.yRotation * kPi / 180) * (primaryZ - s.secondaryFiducial->z);
        const double k = secondaryDistance / cameraDistance;
        drawFiducial(frame, s.secondaryFiducial->x - x + tilted, s.secondaryFiducial->y - y, s.unitsPerPixelX * k, s.unitsPerPixelY * k, true);
    }
    if (s.distortion != 0 || s.yRotation != 0) distort(frame, cameraDistance);
}

void JPImageSource::drawFiducial(JPFrame& frame, double atX, double atY, double uppX, double uppY, bool blurred) const {
    const Settings& s = m_settings;
    const int w = s.width, h = s.height;
    const double cx = w / 2.0, cy = h / 2.0;
    const double r = s.rotation * kPi / 180, cr = std::cos(r), sr = std::sin(r);
    const double sx = s.flipped ? -s.scale : s.scale, sy = s.scale;
    // The view's own transform (OpenPnP's: about the middle, scaled, mirrored, turned), forwards and back.
    auto forward = [&](double px, double py, double& u, double& v) {
        const double qx = px - cx, qy = py - cy;
        u = cx + sx * (cr * qx + sr * qy);
        v = cy + sy * (-sr * qx + cr * qy);
    };
    auto back = [&](double u, double v, double& px, double& py) {
        const double ox = (u - cx) / sx, oy = (v - cy) / sy;
        px = cx + cr * ox - sr * oy;
        py = cy + sr * ox + cr * oy;
    };
    // Where it is, in the view before its transform; 1 mm across, its oval in whole pixels, as OpenPnP draws it.
    const double xc = cx + atX / uppX, yc = cy - atY / uppY;
    const double fw = 1.0 / uppX, fh = 1.0 / uppY;
    double u0, v0, u1, v1;
    forward(xc, yc, u0, v0);
    forward(xc + fw, yc + fh, u1, v1);
    const double dia = std::hypot(u1 - u0, v1 - v0);
    if (u0 + dia < 0 || u1 - dia > w || v0 + dia < 0 || v1 - dia > h) return;
    const int iw = int(fw), ih = int(fh);
    if (iw <= 0 || ih <= 0) return;
    const double ox = xc - fw / 2 + iw / 2.0, oy = yc - fh / 2 + ih / 2.0, rx = iw / 2.0, ry = ih / 2.0;
    // OpenPnP's focal blur: a disc kernel 0.2 mm across in the picture's pixels.
    const double radius = blurred ? 0.2 / s.unitsPerPixelX : 0;
    const int size = int(std::ceil(radius)) * 2 + 1;
    std::vector<double> kernel;
    if (blurred && radius > 0.01) {
        kernel.resize(size_t(size) * size_t(size));
        double sum = 0;
        int num = 0;
        for (size_t i = 0; i < kernel.size(); ++i) {
            const double kx = double(int(i) / size) - size / 2.0 + 0.5, ky = double(int(i) % size) - size / 2.0 + 0.5;
            const double weight = std::clamp(radius + 1 - std::hypot(kx, ky), 0.0, 1.0);
            kernel[i] = weight;
            sum += weight;
            if (weight > 0) ++num;
        }
        if (num > 1) for (double& k : kernel) k /= sum;
        else kernel.clear();
    }
    // Its coverage of each pixel near it (4 x 4 samples a pixel, its edge smoothed as Java's antialiasing).
    const double reach = std::max(rx, ry) * std::abs(s.scale) + size + 2;
    double mu, mv;
    forward(ox, oy, mu, mv);
    const int left = std::max(0, int(std::floor(mu - reach))), right = std::min(w - 1, int(std::ceil(mu + reach)));
    const int top = std::max(0, int(std::floor(mv - reach))), bottom = std::min(h - 1, int(std::ceil(mv + reach)));
    if (left > right || top > bottom) return;
    const int bw = right - left + 1, bh = bottom - top + 1;
    std::vector<double> cover(size_t(bw) * size_t(bh), 0.0);
    constexpr int kSamples = 4;
    for (int v = top; v <= bottom; ++v)
        for (int u = left; u <= right; ++u) {
            int in = 0;
            for (int a = 0; a < kSamples; ++a)
                for (int b = 0; b < kSamples; ++b) {
                    double px, py;
                    back(u + (a + 0.5) / kSamples, v + (b + 0.5) / kSamples, px, py);
                    const double dx = (px - ox) / rx, dy = (py - oy) / ry;
                    if (dx * dx + dy * dy <= 1) ++in;
                }
            cover[size_t(v - top) * size_t(bw) + size_t(u - left)] = double(in) / (kSamples * kSamples);
        }
    if (!kernel.empty()) {
        // Convolved as OpenPnP's (edges left as they are); drawn as its white-on-nothing picture blends.
        std::vector<double> blur(cover.size(), 0.0);
        const int half = size / 2;
        for (int v = 0; v < bh; ++v)
            for (int u = 0; u < bw; ++u) {
                if (u < half || v < half || u >= bw - half || v >= bh - half) {
                    blur[size_t(v) * size_t(bw) + size_t(u)] = cover[size_t(v) * size_t(bw) + size_t(u)];
                    continue;
                }
                double sum = 0;
                for (int i = 0; i < size; ++i)
                    for (int j = 0; j < size; ++j)
                        sum += kernel[size_t(i) * size_t(size) + size_t(j)] * cover[size_t(v + j - half) * size_t(bw) + size_t(u + i - half)];
                blur[size_t(v) * size_t(bw) + size_t(u)] = sum;
            }
        cover.swap(blur);
    }
    for (int v = 0; v < bh; ++v)
        for (int u = 0; u < bw; ++u) {
            const double c = cover[size_t(v) * size_t(bw) + size_t(u)];
            if (c <= 0) continue;
            uint8_t* p = &frame.rgba[(size_t(v + top) * size_t(w) + size_t(u + left)) * kRgba];
            // White over it; blurred, the white fades with its own coverage too (Java's unpremultiplied blur).
            const double white = 255 * (kernel.empty() ? c : c * c);
            for (size_t k = 0; k < 3; ++k) p[k] = uint8_t(std::clamp(white + p[k] * (1 - c), 0.0, 255.0));
            p[3] = 255;
        }
}

void JPImageSource::distort(JPFrame& frame, double cameraDistance) const {
    // OpenPnP's lens distortion and mounting Y rotation: each pixel taken from where the lens and the tilt
    // put it, found by projecting back; the scale staked out first by 9 points so the picture fills the view.
    const Settings& s = m_settings;
    const int width = s.width, height = s.height;
    const std::vector<uint8_t> undistorted = frame.rgba;
    const double xo = 0.5 - width / 2.0, yo = 0.5 - height / 2.0;
    const double radius = std::hypot(width, height) / 2;
    const double dist = cameraDistance / (s.unitsPerPixelX * radius);
    const double factor = 1.0 / radius, zFactor = 1.0 / dist;
    const double yRotRad = s.yRotation * kPi / 180, sinYaw = std::sin(yRotRad), cosYaw = std::cos(yRotRad), tanYaw = sinYaw / cosYaw;
    const double zFactorYaw = zFactor * sinYaw, distort = 0.01 * s.distortion;
    const double zRotRad = s.rotation * kPi / 180, zRotSin = std::sin(zRotRad), zRotCos = std::cos(zRotRad);
    double projectionFactor = radius;
    constexpr int kKernel = 1;
    constexpr uint8_t kGray = 128;
    for (int pass = 0; pass < 2; ++pass) {
        const int xStep = pass == 0 ? width / 2 : 1, yStep = pass == 0 ? height / 2 : 1;
        const int x1 = pass == 0 ? 3 : width - 1, y1 = pass == 0 ? height : height - 1;
        for (int xi = 0; xi < x1; ++xi) {
            const int x = xi * xStep;
            for (int y = 0; y <= y1; y += yStep) {
                const double xN = (x + xo) * factor, yN = (y + yo) * factor;
                const double radial = std::hypot(xN, yN);
                const double distortion = (1 - distort) * radial
                                        + distort * (-0.2 * std::pow(radial, 2) + 0.8 * std::pow(radial, 4) + 0.4 * std::pow(radial, 6));
                const double xD = xN / radial * distortion, yD = yN / radial * distortion;
                const double xR = xD * zRotCos + yD * zRotSin, yR = -xD * zRotSin + yD * zRotCos;
                const double alpha = std::atan2(xR, dist) - yRotRad;
                const double xY = (std::tan(alpha) + tanYaw) * dist;
                const double zT = 1.0 - xY * zFactorYaw;
                const double yY = yR * zT;
                const double xT = xY * zRotCos - yY * zRotSin, yT = xY * zRotSin + yY * zRotCos;
                const double xP = xT * projectionFactor - xo, yP = yT * projectionFactor - yo;
                if (pass == 0) {
                    if (xP < kKernel) projectionFactor = (kKernel + xo) / xT;
                    else if (xP > width - kKernel) projectionFactor = (-kKernel + width + xo) / xT;
                    else if (yP < kKernel) projectionFactor = (kKernel + yo) / yT;
                    else if (yP > height - kKernel) projectionFactor = (-kKernel + height + yo) / yT;
                    continue;
                }
                if (y >= height) continue;
                uint8_t* out = &frame.rgba[(size_t(y) * size_t(width) + size_t(x)) * kRgba];
                const int x0 = int(xP), y0 = int(yP);
                if (x0 >= 0 && x0 + kKernel < width && y0 >= 0 && y0 + kKernel < height) {
                    double rgb[3] = { 0, 0, 0 }, norm = 0;
                    for (int ix = x0; ix <= x0 + kKernel; ++ix)
                        for (int iy = y0; iy <= y0 + kKernel; ++iy) {
                            const uint8_t* in = &undistorted[(size_t(iy) * size_t(width) + size_t(ix)) * kRgba];
                            const double dix = ix - xP, diy = iy - yP;
                            const double weight = std::max(0.0, 1.0 - (dix * dix + diy * diy));
                            norm += weight;
                            for (size_t k = 0; k < 3; ++k) rgb[k] += weight * in[k];
                        }
                    for (size_t k = 0; k < 3; ++k) out[k] = norm > 0 ? uint8_t(std::clamp(int(rgb[k] / norm), 0, 255)) : 0;
                } else {
                    out[0] = out[1] = out[2] = kGray;
                }
                out[3] = 255;
            }
        }
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
