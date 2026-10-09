// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPAutoFocus.h"

#include "JPCameraLook.h"

#include "common/JPlacerLog.h"

#include <j/core/Log.h>

#include <algorithm>
#include <cmath>
#include <thread>
#include <vector>

inline namespace jf {

namespace {

// OpenPnP's: at most this many steps a pass, a millimetre's run-in, a pass's
// end within this many focal resolutions, and the room kept round the circle.
constexpr int    kMaxCurveSteps = 11;
constexpr double kRetractMm = 1.0, kEndSteps = 1.5;
constexpr int    kMarginPx = 50;
// The marks of the diagnostics (OpenPnP's green edges and black rim).
constexpr uint8_t kEdgeMark[4] = { 0, 255, 0, 255 };
constexpr uint8_t kRimMark[4]  = { 0, 0, 0, 255 };
constexpr int kRimPx = 3;

} // namespace

double JPAutoFocus::focusScore(const JPFrame& picture, int diameter, JPFrame* marked) {
    diameter &= ~1;
    const int bands = 3, width = picture.width, height = picture.height;
    const int wCrop = diameter + 1, hCrop = diameter + 1;
    const int xCrop = (width - diameter) / 2, yCrop = (height - diameter) / 2;
    if (diameter <= 0 || xCrop < 0 || yCrop < 0 || xCrop + wCrop > width || yCrop + hCrop > height) return 0;
    // The crop's samples, RGB.
    std::vector<int> samples(size_t(wCrop * hCrop * bands));
    for (int y = 0; y < hCrop; ++y)
        for (int x = 0; x < wCrop; ++x)
            for (int b = 0; b < bands; ++b)
                samples[size_t((y * wCrop + x) * bands + b)] = picture.rgba[size_t(((yCrop + y) * width + xCrop + x) * 4 + b)];
    const int histogramSize = 1 + int(std::ceil(255 * std::sqrt(bands * 2.0)));
    std::vector<int> histogram(size_t(histogramSize), 0);
    const int xNext = bands, yNext = bands * wCrop;
    const int r = diameter / 2, rSquare = r * r, rSquareBracket = (r - kRimPx) * (r - kRimPx);
    if (marked) {
        marked->width = wCrop;
        marked->height = hCrop;
        marked->rgba.assign(size_t(wCrop * hCrop * 4), 0);
        for (int y = 0; y < hCrop; ++y)
            for (int x = 0; x < wCrop; ++x)
                for (int c = 0; c < 4; ++c)
                    marked->rgba[size_t((y * wCrop + x) * 4 + c)] = picture.rgba[size_t(((yCrop + y) * width + xCrop + x) * 4 + c)];
    }
    auto mark = [&](int xi, int yi, const uint8_t* colour) {
        for (int c = 0; c < 4; ++c) marked->rgba[size_t((yi * wCrop + xi) * 4 + c)] = colour[c];
    };
    int over = histogramSize;
    const int passes = marked ? 2 : 1;
    for (int pass = 0; pass < passes; ++pass) {
        size_t i = 0;
        for (int y = -r, yi = 0; y < r; ++y, ++yi) {
            for (int x = -r, xi = 0; x < r; ++x, ++xi) {
                const int distanceSquare = x * x + y * y;
                if (distanceSquare <= rSquare) {
                    double edge = 0;
                    for (int b = 0; b < bands; ++b) {
                        const int xEdge = samples[i] - samples[i + size_t(xNext)];
                        const int yEdge = samples[i] - samples[i + size_t(yNext)];
                        edge += double(xEdge) * xEdge + double(yEdge) * yEdge;
                        ++i;
                    }
                    const int e = int(std::lround(std::sqrt(edge)));
                    histogram[size_t(e)]++;   // on each pass, as OpenPnP's
                    if (marked && e >= over) mark(xi, yi, kEdgeMark);
                } else {
                    i += size_t(xNext);   // outside the circle
                }
                if (marked && pass == passes - 1 && distanceSquare >= rSquareBracket && distanceSquare <= rSquare)
                    mark(xi, yi, kRimMark);
            }
            i += size_t(xNext);   // the edge pixel
        }
        // The hardest edges: as many pixels as the diameter to the power of 1.3, a tenth.
        const int fractile = int(std::pow(diameter, 1.3)) / 10;
        int exceeding = 0;
        for (int h = histogramSize - 1; h >= 0; --h) {
            exceeding += histogram[size_t(h)];
            if (exceeding >= fractile) {
                const double inter = histogram[size_t(h)] > 0 ? (exceeding - fractile) / double(histogram[size_t(h)]) : 0;
                if (pass == passes - 1) return h + inter;
                over = h;
                break;
            }
        }
    }
    return 0;
}

std::optional<double> JPAutoFocus::run(JPCell& cell, JPCameraFeed& feed, const Request& rq,
                                       const std::function<void(const JPFrame&, const std::string&)>& show, std::string& why) {
    if (rq.mmPerPixel <= 0) {
        why = feed.config().name + " is not calibrated: its scale is not known";
        return std::nullopt;
    }
    // The subject's size in pixels, kept within the picture, even.
    JPFrame frame;
    std::string ignored;
    if (!JPCameraLook::settled(feed, frame, why)) return std::nullopt;
    int diameter = int(std::ceil(rq.subjectMaxSizeMm / rq.mmPerPixel));
    diameter = std::min({ diameter, frame.height - kMarginPx, frame.width - kMarginPx }) & ~1;
    // Within the Z axis's soft limits, as the tool's Z.
    auto limited = [&](double z) {
        const JPAxisConfig* a = cell.config().axis(rq.tool.axisZ);
        if (!a) return z;
        if (a->transformed()) {
            if (const JPAxisConfig* in = cell.config().axis(a->inputAxisId)) a = in;
        }
        if (a->softLimitLowEnabled) z = std::max(z, a->softLimitLow + rq.tool.offsetZ);
        if (a->softLimitHighEnabled) z = std::min(z, a->softLimitHigh + rq.tool.offsetZ);
        return z;
    };
    const double toward = rq.z0 >= rq.z1 ? 1 : -1;   // from z1 towards z0, a millimetre past z0
    double z0 = limited(rq.z0), z1 = limited(rq.z1);
    const double speed = rq.settings.focusSpeed * rq.machineSpeed;
    auto at = [&](double z) { return std::array<std::optional<double>, 4> { rq.x, rq.y, z, rq.rotation }; };
    if (!cell.moveToolAndWait(rq.tool, at(limited(z0 + toward * kRetractMm)), 1.0, why)) return std::nullopt;
    const double res = std::max(1e-6, rq.settings.focalResolutionMm);
    JPFrame best;
    while (true) {
        const int curveSteps = std::max(2, std::min(kMaxCurveSteps, 1 + int(std::lround(std::abs(z1 - z0) / res))));
        const double step = (z1 - z0) / (curveSteps - 1);
        std::optional<int> bestStep;
        double bestScore = 0;
        for (int s = 0; s < curveSteps; ++s) {
            const double z = z0 + step * s;
            if (!cell.moveToolStraightAndWait(rq.tool, at(z), speed, why)) return std::nullopt;
            if (!JPCameraLook::settled(feed, frame, why)) {
                if (why.empty()) why = feed.config().name + " gives no picture";
                return std::nullopt;
            }
            JPFrame marked;
            double score = focusScore(frame, diameter, rq.settings.showDiagnostics ? &marked : nullptr);
            uint64_t have = frame.sequence;
            for (int k = 1; k < rq.settings.averagedFrames; ++k) {
                while (!feed.latest(frame, have)) std::this_thread::yield();
                have = frame.sequence;
                score += focusScore(frame, diameter);
            }
            if (!bestStep || bestScore < score) {
                bestStep = s;
                bestScore = score;
                best = marked;
            }
            if (rq.settings.showDiagnostics && show) show(marked, std::string("Auto Focus ") + (*bestStep == s ? "▲" : "▼"));
            JLOGC(JPlacerLog::kCamera, JLogLevel::Trace) << "focus score at Z " << z << " is " << score << ", step " << step;
        }
        if (std::abs(step) / res < kEndSteps) {
            // The focal resolution reached: the best step is the focus.
            const double z = z0 + step * *bestStep;
            if (!cell.moveToolStraightAndWait(rq.tool, at(z), speed, why)) return std::nullopt;
            if (rq.settings.showDiagnostics && show && best.width > 0) show(best, "Auto Focus ⚫");
            return z;
        }
        // The next pass: the steps either side of the best, come into from a millimetre back.
        const double next = std::max(1.0, std::min(curveSteps - 2.0, double(*bestStep)));
        const double old0 = z0;
        z0 = limited(old0 + step * (next - 1));
        z1 = limited(old0 + step * (next + 1));
        if (!cell.moveToolStraightAndWait(rq.tool, at(limited(z0 + toward * kRetractMm)), speed, why)) return std::nullopt;
    }
}

} // inline namespace jf
