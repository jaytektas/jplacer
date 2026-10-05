// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPRectlinearSymmetry.h"

#include <algorithm>
#include <map>
#include <stdexcept>
#include <vector>

inline namespace jf {

namespace {

using Function = JPRectlinearSymmetry::Function;
using Series = std::vector<double>;

// Going finer: the angle searched round the best, in steps of the last.
constexpr double kIterationAngle = 1.1;
// Going finer: this many sub-samplings round the best axis.
constexpr double kIterationRadius = 4;
// Each finer pass samples this many times as closely.
constexpr int kIterationDivision = 4;
// Small subjects are searched at least this large when going finer.
constexpr int kIterationMinSize = 64;

bool isEdge(Function f) { return f == Function::EdgeSymmetry || f == Function::OutlineEdgeSymmetry; }
bool isMasked(Function f) { return f == Function::OutlineSymmetryMasked; }
bool isOutline(Function f) { return f == Function::OutlineSymmetry || f == Function::OutlineEdgeSymmetry || isMasked(f); }
double offsetOf(Function f) { return isEdge(f) ? 0.0 : 0.5; }

// OpenPnP's KernelUtils: an error function (Abramowitz and Stegun 7.1.26).
double erf726(double x) {
    const double a1 = 0.254829592, a2 = -0.284496736, a3 = 1.421413741, a4 = -1.453152027, a5 = 1.061405429, p = 0.3275911;
    const double sign = x > 0 ? 1 : x < 0 ? -1 : 0;
    x = std::abs(x);
    const double t = 1.0 / (1.0 + p * x);
    const double y = 1.0 - (((((a5 * t + a4) * t) + a3) * t + a2) * t + a1) * t * std::exp(-x * x);
    return sign * y;
}

// A Gaussian over whole bins (its integral across each), summing to one.
Series gaussianKernel(double sigma, double mu, int size) {
    auto integral = [&](double x) { return 0.5 * erf726((x - mu) / (std::sqrt(2) * sigma)); };
    Series k(static_cast<size_t>(size), 0.0);
    const double xStart = -(size / 2.0), xEnd = size / 2.0;
    double last = integral(xStart);
    size_t bin = 0;
    for (double xi = xStart; xi < xEnd && bin < k.size(); xi += 1) {
        const double next = integral(xi + 1);
        k[bin++] = next - last;
        last = next;
    }
    double sum = 0;
    for (const double c : k) sum += c;
    for (double& c : k) c /= sum;
    return k;
}

Series kernelOf(Function f, int size) {
    const int sigma = std::max(1, size / 2);
    size |= 1;
    const Series gauss = gaussianKernel(sigma, 0, size);
    if (!isEdge(f)) return gauss;
    // The Gaussian's derivative: edges.
    Series k(size_t(size) + 1);
    for (size_t i = 0; i < size_t(size); ++i) {
        k[i] += gauss[i];
        k[i + 1] -= gauss[i];
    }
    return k;
}

// The kernel run along a cross-section (absolute values), its margins as the nearest.
void applyKernel(int channels, int size, const Series& in, const Series& kernel, Series& out) {
    const int ks = int(kernel.size()), half = ks / 2;
    for (int ch = 0; ch < channels; ++ch) {
        for (int slot = half; slot < size - half; ++slot) {
            double v = 0;
            for (int k = 0; k < ks; ++k) v += in[size_t((slot + k - half) * channels + ch)] * kernel[size_t(k)];
            out[size_t(slot * channels + ch)] = std::abs(v);
        }
        for (int slot = 0; slot < half; ++slot) out[size_t(slot * channels + ch)] = out[size_t(half * channels + ch)];
        for (int slot = size - half; slot < size; ++slot) out[size_t(slot * channels + ch)] = out[size_t((size - half - 1) * channels + ch)];
    }
}

Series applyKernel(int channels, int size, const Series& in, const Series& kernel) {
    Series out(in.size());
    applyKernel(channels, size, in, kernel, out);
    return out;
}

// The contrast along a cross-section: its squared steps summed.
double sumContrast(int channels, int size, const Series& cross, const Series& n) {
    double sum = 0;
    for (int ch = 0; ch < channels; ++ch) {
        std::optional<double> v0;
        for (int x = ch; x < size; ++x) {
            if (n[size_t(x)] <= 0) continue;
            const double v = cross[size_t(x * channels + ch)];
            if (v0) sum += (v - *v0) * (v - *v0);
            v0 = v;
        }
    }
    return sum;
}

// Masked: whether something of the least feature size is there, as a step.
void applyMasked(int channels, int size, double minFeatureSize, int sub, int super, const Series& masked, Series& cross) {
    const double eff = minFeatureSize / sub / super;
    for (int i = 0; i < size; ++i)
        for (int ch = 0; ch < channels; ++ch) cross[size_t(i * channels + ch)] = std::atan(masked[size_t(i)] - eff) + M_PI / 2;
}

// The cross-section's outline from both sides, meeting at the symmetry.
void applyContour(int channels, double s, int symmetryWidth, int size, Series& cross) {
    Series leftMax(static_cast<size_t>(channels)), rightMax(static_cast<size_t>(channels));
    const int symmetry = int(s + symmetryWidth / 2);
    for (int left = 0, right = size - 1; left < size; ++left, --right)
        for (int ch = 0; ch < channels; ++ch) {
            leftMax[size_t(ch)] = std::max(cross[size_t(left * channels + ch)], leftMax[size_t(ch)]);
            rightMax[size_t(ch)] = std::max(cross[size_t(right * channels + ch)], rightMax[size_t(ch)]);
            if (left <= symmetry) cross[size_t(left * channels + ch)] = leftMax[size_t(ch)];
            if (right > symmetry) cross[size_t(right * channels + ch)] = rightMax[size_t(ch)];
        }
}

// The most mirrored middle: the sides' variance against their differences.
std::optional<double> crossSectionSymmetry(int channels, int size, int symmetrySearch, int symmetrySize, Function f, const Series& cross,
                                           JPRectlinearSymmetry::ScoreRange& range) {
    std::optional<double> best;
    double scoreBest = 0;
    for (int s = 0; s <= symmetrySearch; ++s) {
        Series sumL(static_cast<size_t>(channels)), sumLSq(static_cast<size_t>(channels)), sumR(static_cast<size_t>(channels)), sumRSq(static_cast<size_t>(channels)), sumSymSq(static_cast<size_t>(channels));
        Series vLMax(static_cast<size_t>(channels)), vRMax(static_cast<size_t>(channels));
        std::vector<int> n(static_cast<size_t>(channels));
        const int padding = symmetrySearch;
        for (int left = s - padding, right = s + symmetrySize - 1 + padding; left < s + symmetrySize / 2; ++left, --right)
            for (int ch = 0; ch < channels; ++ch) {
                const size_t c = size_t(ch);
                double vL = cross[size_t(std::max(0, left) * channels + ch)];
                double vR = cross[size_t(std::min(size - 1, right) * channels + ch)];
                if (isOutline(f)) {
                    vLMax[c] = std::max(vLMax[c], vL);
                    vL = vLMax[c];
                    vRMax[c] = std::max(vRMax[c], vR);
                    vR = vRMax[c];
                }
                const double dv = std::abs(vL - vR);
                sumL[c] += vL;
                sumLSq[c] += vL * vL;
                sumR[c] += vR;
                sumRSq[c] += vR * vR;
                sumSymSq[c] += dv * dv;
                n[c]++;
            }
        double varL = 0, varR = 0, varSym = 1e-8;
        for (size_t c = 0; c < size_t(channels); ++c) {
            varL += sumLSq[c] - sumL[c] * sumL[c] / n[c];
            varR += sumRSq[c] - sumR[c] * sumR[c] / n[c];
            varSym += sumSymSq[c] / n[c];
        }
        const double score = std::min(varL, varR) / varSym;
        range.add(score);
        if (scoreBest < score) {
            scoreBest = score;
            best = s + offsetOf(f);
        }
    }
    return best;
}

// The bounds: the biggest jump of the outline, both sides alike.
int crossSectionBounds(int channels, double symmetry, int symmetrySize, const Series& cross) {
    double jumpBest = 0;
    int bounds = symmetrySize;
    Series sigL(static_cast<size_t>(channels)), sigR(static_cast<size_t>(channels));
    std::optional<double> signal0;
    for (int half = symmetrySize / 2 - 1; half > 0; --half) {
        const double i = symmetry + symmetrySize / 2 - 1;
        double signal = 0;
        for (int ch = 0; ch < channels; ++ch) {
            sigL[size_t(ch)] = std::max(cross[size_t(int((i - half) * channels + ch))], sigL[size_t(ch)]);
            sigR[size_t(ch)] = std::max(cross[size_t(int((i + half) * channels + ch))], sigR[size_t(ch)]);
            signal += sigL[size_t(ch)] + sigR[size_t(ch)];
        }
        if (signal0 && jumpBest < signal - *signal0) {
            jumpBest = signal - *signal0;
            bounds = half * 2;
        }
        signal0 = signal;
    }
    return bounds;
}

} // namespace

void JPRectlinearSymmetry::ScoreRange::add(double score) {
    if (score <= 0) return;
    minScore = std::min(minScore, score);
    maxScore = std::max(maxScore, score);
    finalScore = std::max(finalScore, score);
}

std::optional<cv::RotatedRect> JPRectlinearSymmetry::find(cv::Mat& image, Search q, ScoreRange& scoreRange) {
    if (image.depth() != CV_8U) throw std::runtime_error("Rectlinear symmetry stage: the image must have 8 bits per channel.");
    if (!image.isContinuous()) image = image.clone();
    const bool innermost = q.subSampling <= std::max(1, -q.superSampling);
    const int channels = image.channels(), width = image.cols, height = image.rows;
    const int maxDim = std::min(width, height);
    const double maxWidth = std::min(q.maxWidth, double(maxDim - q.subSampling * 4));
    const double maxHeight = std::min(q.maxHeight, double(maxDim - q.subSampling * 4));
    const int maxDiagonal = 2 * int(std::ceil(std::sqrt(maxWidth * maxWidth + maxHeight * maxHeight) / 2));
    const double maxSpan = std::max(maxWidth, maxHeight);
    const double searchDistance = std::min(q.searchDistance, maxDim - maxSpan);
    if (searchDistance < q.subSampling) throw std::runtime_error("Image too small for maxWidth, maxHeight");
    const int sub = std::max(1, std::min(q.subSampling, std::min(maxDiagonal / 16, int(searchDistance) / 2)));
    // Super-sampling only works reliably with 2: sampling and the pixel grid interfere.
    const int super = sub == 1 ? std::max(1, std::min(maxDiagonal / 100, q.superSampling)) : 1;
    const int searchDiameter = 2 * int(std::ceil(searchDistance));
    const int r = maxDiagonal / 2;
    const int x0 = std::max(0, (q.xCenter - r - searchDiameter / 2) / sub) * sub;
    const int y0 = std::max(0, (q.yCenter - r - searchDiameter / 2) / sub) * sub;
    const int x1 = std::min(width / sub, (q.xCenter + r + searchDiameter / 2) / sub) * sub;
    const int y1 = std::min(height / sub, (q.yCenter + r + searchDiameter / 2) / sub) * sub;
    const int wPixels = x1 - x0, hPixels = y1 - y0;
    const int cxPixels = q.xCenter - x0, cyPixels = q.yCenter - y0;
    const uchar* pixels = image.ptr<uchar>(y0);
    const int symmetrySearch = (super * searchDiameter / sub / 2) * 2;
    const int symmetryWidth = super * int(maxWidth) / sub;
    const int symmetryHeight = super * int(maxHeight) / sub;
    const int wCross = symmetrySearch + symmetryWidth;
    const int hCross = symmetrySearch + symmetryHeight;
    const double cxCross = double(wCross / 2), cyCross = double(hCross / 2);

    double scoreBest = -INFINITY, angleBest = NAN;
    Series xCross(static_cast<size_t>(wCross * channels)), yCross(static_cast<size_t>(hCross * channels)), xN(static_cast<size_t>(wCross)), yN(static_cast<size_t>(hCross));
    Series xMasked(static_cast<size_t>(wCross)), yMasked(static_cast<size_t>(hCross));
    Series xFiltered(xCross.size()), yFiltered(yCross.size());
    Series xBest = xCross, yBest = yCross, xBestMasked = xMasked, yBestMasked = yMasked;
    // The step depends on the subject's size.
    const double angleStep = std::max(0.0001, std::min(q.searchAngle * M_PI / 180 / 4, sub / maxSpan / super));
    const double a0 = (q.expectedAngle - q.searchAngle) * M_PI / 180;
    const double a1 = (q.expectedAngle + q.searchAngle) * M_PI / 180 + angleStep / 2;
    std::map<double, double> angleScore;
    const Series kernel = gaussianKernel(super, 0, (q.smoothing * super) | 1);
    const double thresholdLuminance = std::pow(q.threshold, q.gamma) * channels;
    // The angle of the most contrast across the rectlinear cross-sections.
    for (double angle = a0; angle <= a1; angle += angleStep) {
        // The reverse turn.
        const double s = super * std::sin(-angle) / sub, c = super * std::cos(-angle) / sub;
        std::fill(xCross.begin(), xCross.end(), 0);
        std::fill(yCross.begin(), yCross.end(), 0);
        std::fill(xN.begin(), xN.end(), 0);
        std::fill(yN.begin(), yN.end(), 0);
        std::fill(xMasked.begin(), xMasked.end(), 0);
        std::fill(yMasked.begin(), yMasked.end(), 0);
        for (int y = 0, dy = -cyPixels, iy = 0; y < hPixels; y += sub, dy += sub, iy += width * channels * sub) {
            const double sy = s * dy, cy = c * dy;
            for (int x = 0, dx = -cxPixels, idx = iy + x0 * channels; x < wPixels; x += sub, dx += sub, idx += channels * sub) {
                const double sx = s * dx, cx = c * dx;
                // Y down.
                const double xc = cx + sy + cxCross, yc = -sx + cy + cyCross;
                const int ix = int(std::floor(xc + 0.5)), iy2 = int(std::floor(yc + 0.5));
                const double xw1 = xc + 0.5 - ix, xw0 = 1 - xw1, yw1 = yc + 0.5 - iy2, yw0 = 1 - yw1;
                if (iy2 <= 1 || iy2 >= hCross || ix <= 1 || ix >= wCross) continue;
                double luminance = 0;
                for (int ch = 0; ch < channels; ++ch) {
                    const size_t xa = size_t(ix * channels + ch), ya = size_t(iy2 * channels + ch);
                    const double pixel = std::pow(double(pixels[idx + ch]), q.gamma);
                    luminance += pixel;
                    xCross[xa] += pixel * xw1;
                    xCross[xa - size_t(channels)] += pixel * xw0;
                    yCross[ya] += pixel * yw1;
                    yCross[ya - size_t(channels)] += pixel * yw0;
                }
                xN[size_t(ix)] += xw1;
                xN[size_t(ix - 1)] += xw0;
                yN[size_t(iy2)] += yw1;
                yN[size_t(iy2 - 1)] += yw0;
                if (luminance > thresholdLuminance) {
                    xMasked[size_t(ix)] += xw1;
                    xMasked[size_t(ix - 1)] += xw0;
                    yMasked[size_t(iy2)] += yw1;
                    yMasked[size_t(iy2 - 1)] += yw0;
                }
            }
        }
        for (int x = 0; x < wCross; ++x)
            if (xN[size_t(x)] > 0)
                for (int ch = 0; ch < channels; ++ch) xCross[size_t(x * channels + ch)] /= xN[size_t(x)];
        for (int y = 0; y < hCross; ++y)
            if (yN[size_t(y)] > 0)
                for (int ch = 0; ch < channels; ++ch) yCross[size_t(y * channels + ch)] /= yN[size_t(y)];
        // Smoothed against interference with the pixel grid, at 45° steps above all.
        applyKernel(channels, wCross, xCross, kernel, xFiltered);
        applyKernel(channels, hCross, yCross, kernel, yFiltered);
        const double contrast = sumContrast(channels, wCross, xFiltered, xN) + sumContrast(channels, hCross, yFiltered, yN);
        if (q.diagnosticsMap) angleScore[angle] = contrast;
        if (scoreBest < contrast) {
            scoreBest = contrast;
            angleBest = angle;
            xBest = xFiltered;
            yBest = yFiltered;
            xBestMasked = xMasked;
            yBestMasked = yMasked;
        }
    }
    if (isMasked(q.xFunction)) applyMasked(channels, wCross, q.minFeatureSize, sub, super, xBestMasked, xBest);
    if (isMasked(q.yFunction)) applyMasked(channels, hCross, q.minFeatureSize, sub, super, yBestMasked, yBest);
    // The symmetry in X and Y.
    Series xSym = applyKernel(channels, wCross, xBest, kernelOf(q.xFunction, 1));
    Series ySym = applyKernel(channels, hCross, yBest, kernelOf(q.yFunction, 1));
    ScoreRange xRange, yRange;
    const std::optional<double> xs = crossSectionSymmetry(channels, wCross, symmetrySearch, symmetryWidth, q.xFunction, xSym, xRange);
    const std::optional<double> ys = crossSectionSymmetry(channels, hCross, symmetrySearch, symmetryHeight, q.yFunction, ySym, yRange);
    // Reset so it says the last pass's; the worse of X and Y.
    scoreRange.finalScore = 0;
    scoreRange.add(std::min(xRange.maxScore, yRange.maxScore));
    std::optional<cv::RotatedRect> rect;
    if (xs && ys) {
        const double xT = *xs - symmetrySearch / 2.0, yT = *ys - symmetrySearch / 2.0;
        const double s = std::sin(angleBest), c = std::cos(angleBest);
        const double xR = xT * c + yT * s, yR = xT * -s + yT * c;
        const double xBestPx = xR * sub / super + q.xCenter, yBestPx = yR * sub / super + q.yCenter;
        int wBest = crossSectionBounds(channels, *xs, symmetryWidth, xBest);
        int hBest = crossSectionBounds(channels, *ys, symmetryHeight, yBest);
        wBest = wBest * sub / super + 2;
        hBest = hBest * sub / super + 2;
        if (wBest < maxWidth - sub * 4 || hBest < maxHeight - sub * 4) {
            rect = cv::RotatedRect(cv::Point2f(float(xBestPx), float(yBestPx)), cv::Size2f(float(wBest), float(hBest)), float(-angleBest * 180 / M_PI));
            if (!innermost) {
                // A small subject in a large span: its angle can be off further.
                const double angleError = angleStep * maxSpan / std::max(wBest, hBest);
                Search local = q;
                local.xCenter = int(xBestPx);
                local.yCenter = int(yBestPx);
                local.expectedAngle = angleBest * 180 / M_PI;
                local.maxWidth = std::min(maxWidth, std::max(kIterationMinSize, wBest) + sub * kIterationRadius * 2);
                local.maxHeight = std::min(maxHeight, std::max(kIterationMinSize, hBest) + sub * kIterationRadius * 2);
                local.searchDistance = sub * kIterationRadius;
                local.searchAngle = angleError * 180 / M_PI * kIterationAngle;
                local.subSampling = sub / kIterationDivision;
                rect = find(image, local, scoreRange);
            }
        }
    }
    if ((innermost && q.diagnostics) || q.diagnosticsMap) {
        // (OpenPnP takes the expected angle in degrees as radians here, when nothing was found.)
        const double angle = rect ? -rect->angle * M_PI / 180 : q.expectedAngle * M_PI / 180;
        const double rx = rect ? rect->center.x : q.xCenter, ry = rect ? rect->center.y : q.yCenter;
        const double rw = rect ? rect->size.width : maxWidth, rh = rect ? rect->size.height : maxHeight;
        const double s = std::sin(angle), c = std::cos(angle);
        const double r0 = std::sqrt(rw * rw + rh * rh) * 0.5 + 20;
        const double r1 = r0 * 1.05 + 40;
        double scoreMin = 0, scoreMax = 0;
        if (!angleScore.empty()) {
            scoreMin = INFINITY, scoreMax = -INFINITY;
            for (const auto& [a, v] : angleScore) scoreMin = std::min(scoreMin, v), scoreMax = std::max(scoreMax, v);
        }
        const double scoreFactor = (r1 - r0) / (scoreMax - scoreMin);
        const double xsScore = xs ? *xs : symmetrySearch / 2.0, ysScore = ys ? *ys : symmetrySearch / 2.0;
        if (isOutline(q.xFunction)) applyContour(channels, xsScore, symmetryWidth, wCross, xSym);
        if (isOutline(q.yFunction)) applyContour(channels, ysScore, symmetryHeight, hCross, ySym);
        Series xMax(static_cast<size_t>(channels)), yMax(static_cast<size_t>(channels));
        for (int i = 0; i < wCross * channels; ++i) xMax[size_t(i % channels)] = std::max(xMax[size_t(i % channels)], xSym[size_t(i)]);
        for (int i = 0; i < hCross * channels; ++i) yMax[size_t(i % channels)] = std::max(yMax[size_t(i % channels)], ySym[size_t(i)]);
        for (int x = 0; x < width; ++x)
            for (int y = 0; y < height; ++y) {
                const double dx = x - rx, dy = y - ry;
                const double dxT = dx * c + dy * -s, dyT = dx * s + dy * c;
                double hairs = 0, angular = 0, cross = 0;
                bool north = false;
                const double ring = std::sqrt(dx * dx + dy * dy);
                if (ring > r1) {
                    // The cross-sections as a map.
                    if (q.diagnosticsMap && innermost) {
                        const double xsT = dxT * super / sub + symmetryWidth / 2 + xsScore, ysT = dyT * super / sub + symmetryHeight / 2 + ysScore;
                        const int xsi = int(std::floor(xsT + 0.5)), ysi = int(std::floor(ysT + 0.5));
                        const double xw1 = xsT - xsi + 0.5, yw1 = ysT - ysi + 0.5;
                        for (int ch = 0; ch < channels; ++ch) {
                            if (xsi > 1 && xsi < wCross - 1)
                                cross = std::max((xSym[size_t(xsi * channels + ch)] * xw1 + xSym[size_t((xsi - 1) * channels + ch)] * (1 - xw1)) / xMax[size_t(ch)], cross);
                            if (ysi > 1 && ysi < hCross - 1)
                                cross = std::max((ySym[size_t(ysi * channels + ch)] * yw1 + ySym[size_t((ysi - 1) * channels + ch)] * (1 - yw1)) / yMax[size_t(ch)], cross);
                        }
                    }
                } else if (ring > r0) {
                    // The contrast by angle, as a graph round it; 0° north, as OpenPnP's cross hairs.
                    if (!angleScore.empty() && ring < r1) {
                        double ringAngle = std::atan2(-dx, -dy);
                        while (ringAngle < a0) ringAngle += 2 * M_PI;
                        while (ringAngle > a1) ringAngle -= 2 * M_PI;
                        auto hi = angleScore.lower_bound(ringAngle);   // ceiling
                        auto lo = angleScore.upper_bound(ringAngle);   // floor: the one before
                        if (hi != angleScore.end() && lo != angleScore.begin()) {
                            --lo;
                            const double da = hi->first - lo->first;
                            const double w1 = da > 0 ? (ringAngle - lo->first) / da : 0, w0 = 1 - w1;
                            const double score = ((w0 * lo->second + w1 * hi->second) - scoreMin) * scoreFactor;
                            angular = std::max(0.0, (0.8 / std::log(M_E * sub)) * std::min(1.0, (r0 + score) - ring));
                        }
                    }
                } else if (innermost && q.diagnostics) {
                    hairs = std::max(std::max(std::max((1 - std::abs(dxT)) * 0.9, (1 - std::abs(dyT)) * 0.9), (1 - std::abs(std::abs(dxT) - rw / 2.0)) * 0.4),
                                     (1 - std::abs(std::abs(dyT) - rh / 2.0)) * 0.4);
                    north = dyT < -rh / 2.0 && std::abs(dxT) < 1;
                }
                if (hairs <= 0 && angular <= 0 && cross <= 0) continue;
                uchar* px = image.ptr<uchar>(y) + x * channels;
                if (channels == 3) {
                    int red = 0, green = 0, blue = 0;
                    if (hairs > 0) {
                        if (scoreRange.finalScore < q.minSymmetry) red = 255;
                        else {
                            green = 255;
                            if (north) red = 255;
                        }
                    }
                    if (angular > 0) red = 255, green = 255, blue = 64;
                    if (cross > 0) blue = 255;
                    const double alpha = std::max(std::max(hairs, angular), cross), compl_ = 1 - alpha;
                    if (alpha > 0) {
                        px[2] = uchar(int(alpha * red + compl_ * px[2]));
                        px[1] = uchar(int(alpha * green + compl_ * px[1]));
                        px[0] = uchar(int(alpha * blue + compl_ * px[0]));
                    }
                } else {
                    const double alpha = std::max(std::max(hairs, angular), cross * 0.5);
                    if (alpha > 0) px[0] = uchar(int(alpha * 255 + (1 - alpha) * px[0]));
                }
            }
    }
    if (scoreRange.finalScore < q.minSymmetry) return std::nullopt;
    return rect;
}

} // inline namespace jf
