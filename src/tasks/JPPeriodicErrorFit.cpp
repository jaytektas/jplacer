// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPPeriodicErrorFit.h"

#include <opencv2/core.hpp>

#include <algorithm>
#include <cmath>

inline namespace jf {

namespace {

// The least-squares solution over the finds kept: the board (a, b, tx, ty: found = [a -b; b a] design + t) and,
// with `wave`, the error along and across (its sin and cos each), the finds that much short.
bool solve(const std::vector<JPPeriodicErrorFit::Find>& finds, const std::vector<bool>& kept, double k, bool alongY,
           bool wave, cv::Mat& p) {
    const int n = int(std::count(kept.begin(), kept.end(), true));
    const int cols = wave ? 8 : 4;
    cv::Mat A = cv::Mat::zeros(2 * n, cols, CV_64F), r(2 * n, 1, CV_64F);
    int row = 0;
    for (size_t i = 0; i < finds.size(); ++i) {
        if (!kept[i]) continue;
        const auto& f = finds[i];
        const double s = std::sin(k * f.at), c = std::cos(k * f.at);
        // X row, then Y row.
        double* x = A.ptr<double>(row);
        x[0] = f.designX;
        x[1] = -f.designY;
        x[2] = 1;
        double* y = A.ptr<double>(row + 1);
        y[0] = f.designY;
        y[1] = f.designX;
        y[3] = 1;
        if (wave) {
            // along (4, 5) and across (6, 7), subtracted from where it would be found.
            double* alongRow = alongY ? y : x;
            double* acrossRow = alongY ? x : y;
            alongRow[4] = -s;
            alongRow[5] = -c;
            acrossRow[6] = -s;
            acrossRow[7] = -c;
        }
        r.at<double>(row) = f.foundX;
        r.at<double>(row + 1) = f.foundY;
        row += 2;
    }
    return cv::solve(A, r, p, cv::DECOMP_SVD);
}

} // namespace

bool JPPeriodicErrorFit::fit(const std::vector<Find>& finds, double periodMm, bool alongY, JPPeriodicErrorConfig::Scan& scan,
                             std::string& why) {
    if (periodMm <= 0) {
        why = "the drive's period is not set";
        return false;
    }
    if (finds.size() < kLeastFinds) {
        why = std::to_string(finds.size()) + " footprints found; at least " + std::to_string(kLeastFinds) + " are needed";
        return false;
    }
    const double k = 2 * M_PI / periodMm;
    std::vector<bool> kept(finds.size(), true);
    cv::Mat p;
    // How far each find is off the fit (mm), the board and the wave as solved.
    auto offBy = [&](const cv::Mat& q, bool wave, size_t i, double& dx, double& dy) {
        const auto& f = finds[i];
        const double a = q.at<double>(0), b = q.at<double>(1);
        double ex = 0, ey = 0;
        if (wave) {
            const double s = std::sin(k * f.at), c = std::cos(k * f.at);
            const double along = q.at<double>(4) * s + q.at<double>(5) * c, across = q.at<double>(6) * s + q.at<double>(7) * c;
            (alongY ? ey : ex) = along;
            (alongY ? ex : ey) = across;
        }
        dx = f.foundX - (a * f.designX - b * f.designY + q.at<double>(2) - ex);
        dy = f.foundY - (b * f.designX + a * f.designY + q.at<double>(3) - ey);
    };
    for (int round = 0; round < 4; ++round) {
        if (std::count(kept.begin(), kept.end(), true) < long(kLeastFinds)) {
            why = "too many footprints were found far from the rest to fit (" + std::to_string(std::count(kept.begin(), kept.end(), true))
                + " left)";
            return false;
        }
        if (!solve(finds, kept, k, alongY, true, p)) {
            why = "the footprints found cannot be fitted";
            return false;
        }
        std::vector<double> d;
        for (size_t i = 0; i < finds.size(); ++i) {
            double dx = 0, dy = 0;
            offBy(p, true, i, dx, dy);
            if (kept[i]) d.push_back(std::hypot(dx, dy));
        }
        std::nth_element(d.begin(), d.begin() + long(d.size() / 2), d.end());
        const double limit = std::max(kLeastOutlierMm, kOutlierSpread * d[d.size() / 2]);
        bool changed = false;
        for (size_t i = 0; i < finds.size(); ++i) {
            double dx = 0, dy = 0;
            offBy(p, true, i, dx, dy);
            const bool keep = std::hypot(dx, dy) <= limit;
            changed = changed || keep != kept[i];
            kept[i] = keep;
        }
        if (!changed) break;
    }
    cv::Mat board;   // the board alone, no wave: how far off the finds are without it
    if (!solve(finds, kept, k, alongY, false, board)) {
        why = "the footprints found cannot be fitted";
        return false;
    }
    double before = 0, after = 0;
    int n = 0;
    scan.points.clear();
    for (size_t i = 0; i < finds.size(); ++i) {
        double bx = 0, by = 0, ax = 0, ay = 0;
        offBy(board, false, i, bx, by);
        offBy(p, true, i, ax, ay);
        if (kept[i]) {
            before += bx * bx + by * by;
            after += ax * ax + ay * ay;
            ++n;
        }
        // As the wave shows it: how far the head was off (the find that much short), along and across; and the fit's.
        const double s = std::sin(k * finds[i].at), c = std::cos(k * finds[i].at);
        double wx = 0, wy = 0;
        offBy(p, false, i, wx, wy);   // the board alone from the full fit: what is left is the wave and the noise
        JPPeriodicErrorConfig::Scan::Point q;
        q.at = finds[i].at;
        q.along = -(alongY ? wy : wx);
        q.across = -(alongY ? wx : wy);
        q.alongFit = p.at<double>(4) * s + p.at<double>(5) * c;
        q.acrossFit = p.at<double>(6) * s + p.at<double>(7) * c;
        q.outlier = !kept[i];
        scan.points.push_back(q);
    }
    scan.periodMm = periodMm;
    scan.alongSin = p.at<double>(4);
    scan.alongCos = p.at<double>(5);
    scan.acrossSin = p.at<double>(6);
    scan.acrossCos = p.at<double>(7);
    scan.scale = std::hypot(p.at<double>(0), p.at<double>(1));
    scan.rotationDeg = std::atan2(p.at<double>(1), p.at<double>(0)) * 180 / M_PI;
    scan.rmsBefore = std::sqrt(before / std::max(1, n));
    scan.rmsAfter = std::sqrt(after / std::max(1, n));
    scan.applied = false;
    return true;
}

} // inline namespace jf
