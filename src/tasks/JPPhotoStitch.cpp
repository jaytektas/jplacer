// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPPhotoStitch.h"

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <optional>
#include <utility>

inline namespace jf {

namespace {

// How much of an overlap's edge is left out of comparing it (its share each side): the pictures' own edges.
constexpr double kOverlapInset = 0.02;
// How firmly each picture is held where the machine put it, against its pairs (least squares): only enough to fix
// the whole lot in place.
constexpr double kAnchor = 1e-3;

// A picture's four corners (pixels).
std::array<std::pair<double, double>, 4> corners(const cv::Mat& m) {
    return { { { 0.0, 0.0 }, { double(m.cols), 0.0 }, { 0.0, double(m.rows) }, { double(m.cols), double(m.rows) } } };
}

struct Box {
    double minX = 0, minY = 0, maxX = 0, maxY = 0;
};

Box machineBox(const JPPhotoStitch::Tile& t) {
    Box b { 1e300, 1e300, -1e300, -1e300 };
    for (const auto& [px, py] : corners(t.bgr)) {
        double x = 0, y = 0;
        t.toMachine.apply(px, py, x, y);
        b.minX = std::min(b.minX, x);
        b.minY = std::min(b.minY, y);
        b.maxX = std::max(b.maxX, x);
        b.maxY = std::max(b.maxY, y);
    }
    return b;
}

cv::Mat toCv(const JPAffineTransform& a) {
    return (cv::Mat_<double>(2, 3) << a.m00, a.m01, a.m02, a.m10, a.m11, a.m12);
}

// A grid's pixel (column, row) to a place: from `x0`, `y0` at its top-left, rows going down Y.
JPAffineTransform grid(double x0, double y0, double mmPerPixel) {
    JPAffineTransform g;
    g.m00 = mmPerPixel;
    g.m11 = -mmPerPixel;
    g.m02 = x0;
    g.m12 = y0;
    return g;
}

// The picture as seen on a grid (`toTile`: a grid pixel to the picture's), grey, in floating point.
cv::Mat greyOn(const cv::Mat& bgr, const JPAffineTransform& toTile, cv::Size size) {
    cv::Mat grey, warped;
    cv::cvtColor(bgr, grey, cv::COLOR_BGR2GRAY);
    cv::warpAffine(grey, warped, toCv(toTile), size, cv::INTER_LINEAR | cv::WARP_INVERSE_MAP, cv::BORDER_REPLICATE);
    cv::Mat f;
    warped.convertTo(f, CV_32F);
    return f;
}

// Each pixel's weight in a blend: how far in from the picture's nearest edge it is.
cv::Mat feather(cv::Size size) {
    cv::Mat w(size, CV_32F);
    for (int y = 0; y < size.height; ++y) {
        float* row = w.ptr<float>(y);
        const float dy = std::min(y + 0.5f, size.height - y - 0.5f);
        for (int x = 0; x < size.width; ++x) row[x] = std::min(dy, std::min(x + 0.5f, size.width - x - 0.5f));
    }
    return w;
}

} // namespace

bool JPPhotoStitch::shiftBetween(const Tile& a, const Tile& b, double mmPerPixel, double& dx, double& dy, double& response) {
    const auto ia = a.toMachine.inverse();
    const auto ib = b.toMachine.inverse();
    if (!ia || !ib || mmPerPixel <= 0) return false;
    const Box ba = machineBox(a), bb = machineBox(b);
    Box o { std::max(ba.minX, bb.minX), std::max(ba.minY, bb.minY), std::min(ba.maxX, bb.maxX), std::min(ba.maxY, bb.maxY) };
    const double insetX = (o.maxX - o.minX) * kOverlapInset, insetY = (o.maxY - o.minY) * kOverlapInset;
    o = { o.minX + insetX, o.minY + insetY, o.maxX - insetX, o.maxY - insetY };
    const int w = int((o.maxX - o.minX) / mmPerPixel), h = int((o.maxY - o.minY) / mmPerPixel);
    if (w < kMinOverlapPx || h < kMinOverlapPx) return false;
    const JPAffineTransform g = grid(o.minX, o.maxY, mmPerPixel);
    JPAffineTransform toA = *ia, toB = *ib;
    toA.concatenate(g);
    toB.concatenate(g);
    const cv::Mat ga = greyOn(a.bgr, toA, { w, h }), gb = greyOn(b.bgr, toB, { w, h });
    // Too plain to tell: nothing to line up by (a response of 0).
    cv::Scalar mean, deviation;
    for (const cv::Mat* g : { &ga, &gb }) {
        cv::meanStdDev(*g, mean, deviation);
        if (deviation[0] < kMinContrast) {
            dx = dy = response = 0;
            return true;
        }
    }
    cv::Mat window;
    cv::createHanningWindow(window, { w, h }, CV_32F);
    const cv::Point2d s = cv::phaseCorrelate(ga, gb, window, &response);
    // On the grid, b's content lies `s` from a's; rows go down Y.
    dx = s.x * mmPerPixel;
    dy = -s.y * mmPerPixel;
    return true;
}

bool JPPhotoStitch::stitch(const std::vector<Tile>& given, const JPAffineTransform& boardToMachine, const Area& area,
                           double mmPerPixel, Result& result, std::string& why) {
    result = Result();
    std::vector<Tile> tiles;
    for (const Tile& t : given)
        if (!t.bgr.empty() && t.toMachine.inverse()) tiles.push_back(t);
    if (tiles.empty()) {
        why = "no picture to make the photo of";
        return false;
    }
    if (mmPerPixel <= 0 || area.maxX <= area.minX || area.maxY <= area.minY) {
        why = "nothing to photograph (the board's area is empty)";
        return false;
    }

    // Lined up: each overlapping pair's shift, then the corrections that agree with them best.
    struct Pair {
        size_t a, b;
        double dx, dy;
    };
    std::vector<Pair> pairs;
    for (size_t i = 0; i < tiles.size(); ++i)
        for (size_t j = i + 1; j < tiles.size(); ++j) {
            double dx = 0, dy = 0, response = 0;
            if (!shiftBetween(tiles[i], tiles[j], mmPerPixel, dx, dy, response)) continue;
            ++result.pairs;
            if (response < kMinResponse || std::hypot(dx, dy) > kMaxShiftMm) continue;
            pairs.push_back({ i, j, dx, dy });
        }
    result.lined = int(pairs.size());
    const int n = int(tiles.size());
    std::vector<double> cx(size_t(n), 0.0), cy(size_t(n), 0.0);
    if (!pairs.empty()) {
        // b moved by -shift against a: c_b - c_a = -d, each pair weighed alike.
        cv::Mat l = cv::Mat::eye(n, n, CV_64F) * kAnchor;
        cv::Mat rx = cv::Mat::zeros(n, 1, CV_64F), ry = cv::Mat::zeros(n, 1, CV_64F);
        for (const Pair& p : pairs) {
            const int a = int(p.a), b = int(p.b);
            l.at<double>(a, a) += 1;
            l.at<double>(b, b) += 1;
            l.at<double>(a, b) -= 1;
            l.at<double>(b, a) -= 1;
            rx.at<double>(b) -= p.dx;
            rx.at<double>(a) += p.dx;
            ry.at<double>(b) -= p.dy;
            ry.at<double>(a) += p.dy;
        }
        cv::Mat sx, sy;
        cv::solve(l, rx, sx, cv::DECOMP_CHOLESKY);
        cv::solve(l, ry, sy, cv::DECOMP_CHOLESKY);
        for (int i = 0; i < n; ++i) {
            cx[size_t(i)] = sx.at<double>(i);
            cy[size_t(i)] = sy.at<double>(i);
            result.largestCorrectionMm = std::max(result.largestCorrectionMm, std::hypot(cx[size_t(i)], cy[size_t(i)]));
        }
        for (const Pair& p : pairs)
            result.largestDisagreementMm = std::max(result.largestDisagreementMm,
                                                    std::hypot(cx[p.b] - cx[p.a] + p.dx, cy[p.b] - cy[p.a] + p.dy));
    }

    // The photo: a pixel to the board, to the machine, to each picture (moved as lined up).
    const int width = int(std::ceil((area.maxX - area.minX) / mmPerPixel));
    const int height = int(std::ceil((area.maxY - area.minY) / mmPerPixel));
    result.mmPerPixel = mmPerPixel;
    result.originX = area.minX + mmPerPixel / 2;
    result.originY = area.maxY - mmPerPixel / 2;
    const JPAffineTransform toBoard = grid(result.originX, result.originY, mmPerPixel);
    struct Placed {
        const Tile*       tile;
        JPAffineTransform toTile;    // a photo pixel to the picture's
        cv::Rect          footprint; // in the photo
    };
    std::vector<Placed> placed;
    for (int i = 0; i < n; ++i) {
        JPAffineTransform moved = tiles[size_t(i)].toMachine;
        JPAffineTransform shift;
        shift.translate(cx[size_t(i)], cy[size_t(i)]);
        moved.preConcatenate(shift);
        auto inv = moved.inverse();
        if (!inv) continue;
        JPAffineTransform toTile = *inv;
        toTile.concatenate(boardToMachine);
        toTile.concatenate(toBoard);
        const auto fromTile = toTile.inverse();
        if (!fromTile) continue;
        double minX = 1e300, minY = 1e300, maxX = -1e300, maxY = -1e300;
        const Tile& t = tiles[size_t(i)];
        for (const auto& [px, py] : corners(t.bgr)) {
            double x = 0, y = 0;
            fromTile->apply(px, py, x, y);
            minX = std::min(minX, x);
            minY = std::min(minY, y);
            maxX = std::max(maxX, x);
            maxY = std::max(maxY, y);
        }
        const cv::Rect box = cv::Rect(int(std::floor(minX)), int(std::floor(minY)), int(std::ceil(maxX - minX)) + 1,
                                      int(std::ceil(maxY - minY)) + 1) & cv::Rect(0, 0, width, height);
        if (box.area() > 0) placed.push_back({ &t, toTile, box });
    }
    if (placed.empty()) {
        why = "no picture lies on the board";
        return false;
    }

    result.bgra = cv::Mat(height, width, CV_8UC4, cv::Scalar(0, 0, 0, 0));
    std::map<std::pair<int, int>, cv::Mat> weights;   // by picture size
    long long uncovered = 0;
    for (int r0 = 0; r0 < height; r0 += kStripRows) {
        const int rows = std::min(kStripRows, height - r0);
        cv::Mat sum = cv::Mat::zeros(rows, width, CV_32FC3), weight = cv::Mat::zeros(rows, width, CV_32F);
        for (const Placed& p : placed) {
            const cv::Rect roi = p.footprint & cv::Rect(0, r0, width, rows);
            if (roi.area() <= 0) continue;
            JPAffineTransform at = p.toTile;
            at.translate(roi.x, roi.y);
            const cv::Mat m = toCv(at);
            cv::Mat& w = weights[{ p.tile->bgr.cols, p.tile->bgr.rows }];
            if (w.empty()) w = feather(p.tile->bgr.size());
            cv::Mat colour, wOn;
            cv::warpAffine(p.tile->bgr, colour, m, roi.size(), cv::INTER_LINEAR | cv::WARP_INVERSE_MAP, cv::BORDER_CONSTANT);
            cv::warpAffine(w, wOn, m, roi.size(), cv::INTER_LINEAR | cv::WARP_INVERSE_MAP, cv::BORDER_CONSTANT, cv::Scalar(0));
            cv::Mat cf;
            colour.convertTo(cf, CV_32FC3);
            cv::Mat w3;
            cv::merge(std::vector<cv::Mat> { wOn, wOn, wOn }, w3);
            const cv::Rect local(roi.x, roi.y - r0, roi.width, roi.height);
            sum(local) += cf.mul(w3);
            weight(local) += wOn;
        }
        for (int y = 0; y < rows; ++y) {
            const cv::Vec3f* s = sum.ptr<cv::Vec3f>(y);
            const float* w = weight.ptr<float>(y);
            cv::Vec4b* out = result.bgra.ptr<cv::Vec4b>(r0 + y);
            for (int x = 0; x < width; ++x) {
                if (w[x] <= 0) {
                    ++uncovered;
                    continue;
                }
                const cv::Vec3f c = s[x] / w[x];
                out[x] = { cv::saturate_cast<uint8_t>(c[0]), cv::saturate_cast<uint8_t>(c[1]), cv::saturate_cast<uint8_t>(c[2]), 255 };
            }
        }
    }
    result.uncoveredShare = double(uncovered) / (double(width) * height);
    return true;
}

} // inline namespace jf
