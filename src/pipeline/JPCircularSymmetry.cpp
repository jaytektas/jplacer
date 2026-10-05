// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPCircularSymmetry.h"

#include <algorithm>
#include <stdexcept>

inline namespace jf {

namespace {

using Circle = JPPipelineModel::Circle;

// The local search when going finer: this many sub-samplings round the best.
constexpr int kIterationRadius = 2;
// Each finer pass samples this many times as closely.
constexpr int kIterationDivision = 4;
// The score's least possible value.
constexpr double kScoreFloor = 0;
// The least symmetry still searched finer.
constexpr double kAbsoluteMinSymmetry = 0.1;
// Candidates kept to search finer, for each target wanted.
constexpr int kIterationTargetsFactor = 2;
// Keeps a variance ratio of perfect pictures finite.
constexpr double kDiv0Guard = 0.1;

std::vector<Circle> sortAndLimit(std::vector<Circle> circles, int maxTargetCount, double corrSymmetry) {
    std::stable_sort(circles.begin(), circles.end(), [](const Circle& a, const Circle& b) { return a.score > b.score; });
    std::vector<Circle> out;
    if (circles.empty()) return out;
    const double minScore = circles.front().score * corrSymmetry;
    for (const Circle& c : circles) {
        if (c.score < minScore) break;
        out.push_back(c);
        if (int(out.size()) >= maxTargetCount) break;
    }
    return out;
}

} // namespace

void JPCircularSymmetry::ScoreRange::add(double score) {
    if (score <= kScoreFloor) return;
    minScore = std::min(minScore, score);
    maxScore = std::max(maxScore, score);
    finalScore = std::max(finalScore, score);
    m_sum += score;
    ++m_n;
}

double JPCircularSymmetry::ScoreRange::heat(double score) const {
    const double range = maxScore - minScore;
    const double avg = (m_sum / m_n - minScore) / range;
    const double s = (score - minScore) / range;
    return std::pow(s, std::log(0.71) / std::log(avg));
}

std::vector<Circle> JPCircularSymmetry::find(cv::Mat& image, Search q, ScoreRange& scoreRange) {
    if (image.depth() != CV_8U) throw std::runtime_error("Circular symmetry stage: the image must have 8 bits per channel.");
    if (!image.isContinuous()) image = image.clone();
    const bool outermost = !std::isfinite(scoreRange.finalScore);
    const int channels = image.channels(), width = image.cols, height = image.rows;
    // Odd diameters, a little apart.
    const int minDiameter = std::max(3, q.minDiameter | 1);
    const int maxDiameter = std::max(minDiameter + 4, q.maxDiameter | 1);
    const int superSampling = std::min(16, q.superSampling);
    // Finer sampling for a fine edge.
    const int sub = std::max(1, std::min(q.subSampling, std::min((maxDiameter - minDiameter) / 4, minDiameter / 2)));
    const int rSearch = q.searchDiameter / 2 + 1;
    const int rSearchSq = rSearch * rSearch;
    const int r = maxDiameter / 2;
    const int r0 = minDiameter / 2 - 1;
    // The search range, within the picture.
    const int x0Range = std::max(0, (q.xCenter - r - q.searchWidth / 2) / sub) * sub;
    const int y0Range = std::max(0, (q.yCenter - r - q.searchHeight / 2) / sub) * sub;
    const int x1Range = std::min((width - maxDiameter) / sub, (q.xCenter - r + q.searchWidth / 2 + sub / 2) / sub) * sub;
    const int y1Range = std::min((height - maxDiameter) / sub, (q.yCenter - r + q.searchHeight / 2 + sub / 2) / sub) * sub;
    const int xSearch = q.xCenter - r - x0Range;
    const int ySearch = q.yCenter - r - y0Range;
    const int wRange = x1Range - x0Range;
    const int hRange = y1Range - y0Range;
    if (wRange < 1 || hRange < 1) throw std::runtime_error("Circular symmetry stage: search range is cropped to nothing.");
    // Sub-pixel offsets on the final pass.
    std::vector<double> offsets { 0.0 };
    const bool finalPass = sub == 1 && (q.searchDiameter <= kIterationRadius || superSampling <= 1);
    if (finalPass && superSampling > 1) {
        offsets.clear();
        for (int s = 0; s < superSampling; ++s) offsets.push_back(double(s) / superSampling - 0.5);
    }
    const uchar* pixels = image.ptr<uchar>(y0Range);

    double scoreBest = -INFINITY, xBest = 0, yBest = 0;
    int rContrastBest = 0;
    const bool showDiagnostics = (q.diagnostics || q.heatMap) && offsets.size() == 1;
    const int wMap = wRange / sub, hMap = hRange / sub;
    std::vector<double> scoreMap, xOffsetMap, yOffsetMap;
    std::vector<int> radiusMap;
    if (showDiagnostics || q.maxTargetCount > 1) {
        scoreMap.assign(size_t(wMap) * size_t(hMap), -INFINITY);
        radiusMap.assign(scoreMap.size(), 0);
        xOffsetMap.assign(scoreMap.size(), 0);
        yOffsetMap.assign(scoreMap.size(), 0);
        // So it says the last pass's best.
        scoreRange.finalScore = 0;
    }
    const int dim = maxDiameter / sub + 1;
    const int maxPixelData = dim * dim * channels;
    const int rDim = (r - r0 + 1) / sub;
    const int angleDim = q.score == Score::RingMedianVarianceVsRingVarianceSum ? 16 : 1;
    const int angleMask = angleDim - 1;
    const double fa = angleDim / (M_PI * 2);
    const int histogramDim = angleDim * rDim * channels;
    std::vector<int> idxPixelData(static_cast<size_t>(maxPixelData)), idxHistogram(static_cast<size_t>(maxPixelData)), rRing(static_cast<size_t>(rDim));
    std::vector<double> segmentValues(static_cast<size_t>(angleDim));
    for (int ri = 0; ri < rDim; ++ri) rRing[size_t(ri)] = r0 + ri * sub;
    std::vector<int> histogramN(static_cast<size_t>(histogramDim));
    std::vector<double> histogramFactor(static_cast<size_t>(histogramDim));
    std::vector<long long> histogramSum(static_cast<size_t>(histogramDim)), histogramSumSq(static_cast<size_t>(histogramDim));

    for (const double xOffset : offsets)
        for (const double yOffset : offsets) {
            double scoreBestSampling = -INFINITY;
            // The rings (and angular bins), as indices into the pixels from the
            // circle's left upper corner: valid wherever it is moved.
            int samples = 0;
            std::fill(histogramN.begin(), histogramN.end(), 0);
            for (int y = -r, yi = 0; y <= r; y += sub, yi += sub)
                for (int x = -r, idx = yi * width * channels; x <= r; x += sub, idx += channels * sub) {
                    const double dx = x - xOffset, dy = y - yOffset;
                    const double d = std::hypot(dx, dy);
                    const int idxR = (-r0 + int(std::floor(d + 0.5))) / sub;
                    if (idxR < 0 || idxR >= rDim) continue;
                    const double angle = angleMask == 0 ? 0 : std::atan2(dy, dx);
                    const int idxAngle = angleMask & int(std::floor(angle * fa + 0.5));
                    const int idxHisto = (idxR * angleDim + idxAngle) * channels;
                    for (int ch = 0; ch < channels; ++ch) {
                        idxPixelData[size_t(samples)] = idx + ch;
                        idxHistogram[size_t(samples)] = idxHisto + ch;
                        histogramN[size_t(idxHisto + ch)]++;
                        ++samples;
                    }
                }
            for (int i = 0; i < histogramDim; ++i) histogramFactor[size_t(i)] = histogramN[size_t(i)] > 0 ? 1.0 / histogramN[size_t(i)] : 0;
            // Every place: its symmetry.
            for (int yi = 0, yis = 0; yi < hRange; yi += sub, ++yis)
                for (int xi = 0, xis = 0, idxOffset = (yi * width + x0Range) * channels; xi < wRange;
                     xi += sub, ++xis, idxOffset += channels * sub) {
                    const int distSq = (xi - xSearch) * (xi - xSearch) + (yi - ySearch) * (yi - ySearch);
                    if (distSq > rSearchSq) continue;
                    std::fill(histogramSum.begin(), histogramSum.end(), 0);
                    std::fill(histogramSumSq.begin(), histogramSumSq.end(), 0);
                    for (int i = 0; i < samples; ++i) {
                        const long long pixel = pixels[idxOffset + idxPixelData[size_t(i)]];
                        const size_t h = size_t(idxHistogram[size_t(i)]);
                        histogramSum[h] += pixel;
                        histogramSumSq[h] += pixel * pixel;
                    }
                    // Variances weighed by their pixel counts: (SumSq − Sum² / n).
                    double contrastBest = -INFINITY;
                    int riContrastBest = 0;
                    double varianceRing = 0;
                    std::vector<double> sumAcross(static_cast<size_t>(channels)), sumSqAcross(static_cast<size_t>(channels)), lastAvg(static_cast<size_t>(channels));
                    std::vector<int> nAcross(static_cast<size_t>(channels));
                    for (int idxR = 0; idxR < rDim; ++idxR) {
                        double contrast = 0;
                        for (int ch = 0; ch < channels; ++ch) {
                            double sumRing = 0, sumSqRing = 0;
                            int nRing = 0;
                            const size_t c = size_t(ch);
                            switch (q.score) {
                                case Score::OverallVarianceVsRingVarianceSum: {
                                    const size_t h = size_t((idxR * angleDim) * channels + ch);
                                    sumRing += double(histogramSum[h]);
                                    sumSqRing += double(histogramSumSq[h]);
                                    nRing += histogramN[h];
                                    varianceRing += sumSqRing - sumRing * sumRing / nRing;
                                    sumAcross[c] += sumRing;
                                    sumSqAcross[c] += sumSqRing;
                                    break;
                                }
                                case Score::RingAvgeragesVarianceVsRingVarianceSum: {
                                    for (int a = 0; a < angleDim; ++a) {
                                        const size_t h = size_t((idxR * angleDim + a) * channels + ch);
                                        const int n = histogramN[h];
                                        const double segmentAvg = double(histogramSum[h]) * histogramFactor[h];
                                        sumRing += double(histogramSum[h]);
                                        sumSqRing += double(histogramSumSq[h]);
                                        sumSqAcross[c] += segmentAvg * segmentAvg * n;
                                        nRing += n;
                                    }
                                    sumAcross[c] += sumRing;
                                    varianceRing += sumSqRing - sumRing * sumRing / nRing;
                                    break;
                                }
                                case Score::RingMedianVarianceVsRingVarianceSum: {
                                    int slot = 0;
                                    for (int a = 0; a < angleDim; ++a) {
                                        const size_t h = size_t((idxR * angleDim + a) * channels + ch);
                                        const int n = histogramN[h];
                                        if (n <= 0) continue;
                                        segmentValues[size_t(slot++)] = double(histogramSum[h]) * histogramFactor[h];
                                        sumRing += double(histogramSum[h]);
                                        sumSqRing += double(histogramSumSq[h]);
                                        nRing += n;
                                    }
                                    std::sort(segmentValues.begin(), segmentValues.begin() + slot);
                                    const double median = (segmentValues[size_t(std::max(0, slot / 2 - 1))] + segmentValues[size_t(slot / 2)]) * 0.5;
                                    sumAcross[c] += median * nRing;
                                    sumSqAcross[c] += median * median * nRing;
                                    varianceRing += sumSqRing - sumRing * sumRing / nRing;
                                    break;
                                }
                            }
                            nAcross[c] += nRing;
                            const double avg1 = sumRing / nRing;
                            contrast += (lastAvg[c] - avg1) * (lastAvg[c] - avg1);
                            lastAvg[c] = avg1;
                        }
                        if (rRing[size_t(idxR)] * 2 >= minDiameter && contrastBest < contrast) {
                            contrastBest = contrast;
                            riContrastBest = rRing[size_t(idxR)];
                        }
                    }
                    double varianceAcross = 0;
                    for (int ch = 0; ch < channels; ++ch)
                        varianceAcross += sumSqAcross[size_t(ch)] - sumAcross[size_t(ch)] * sumAcross[size_t(ch)] / nAcross[size_t(ch)];
                    const double score = (varianceAcross + kDiv0Guard) / (varianceRing + kDiv0Guard);
                    scoreRange.add(score);
                    if (scoreBestSampling < score) {
                        scoreBestSampling = score;
                        if (scoreBest < score) {
                            scoreBest = score;
                            xBest = xi + x0Range + r + 0.5 + xOffset;
                            yBest = yi + y0Range + r + 0.5 + yOffset;
                            rContrastBest = riContrastBest;
                        }
                    }
                    if (!scoreMap.empty()) {
                        const size_t idx = size_t(yis * wMap + xis);
                        if (scoreMap[idx] < score) {
                            scoreMap[idx] = score;
                            radiusMap[idx] = riContrastBest;
                            xOffsetMap[idx] = xOffset;
                            yOffsetMap[idx] = yOffset;
                        }
                    }
                }
        }

    std::vector<Circle> ret;
    auto finer = [&](double x, double y, int count) {
        Search local = q;
        local.xCenter = int(x);
        local.yCenter = int(y);
        local.searchDiameter = local.searchWidth = local.searchHeight = sub * kIterationRadius;
        local.maxTargetCount = count;
        local.subSampling = sub / kIterationDivision;
        local.minDiameter = minDiameter;
        local.maxDiameter = maxDiameter;
        return find(image, local, scoreRange);
    };
    if (q.maxTargetCount > 1) {
        const double minSymmetryEff = sub == 1 ? q.minSymmetry : kAbsoluteMinSymmetry;
        if (scoreBest > minSymmetryEff) {
            // The score map's peaks.
            std::vector<Circle> maxima;
            for (int yis = 1; yis < hMap - 1; ++yis)
                for (int xis = 1; xis < wMap - 1; ++xis) {
                    auto at = [&](int dx, int dy) { return scoreMap[size_t((yis + dy) * wMap + xis + dx)]; };
                    const double score = at(0, 0);
                    if (score > minSymmetryEff && at(-1, 0) < score && at(1, 0) < score && at(-1, -1) < score && at(0, -1) < score
                        && at(1, -1) < score && at(-1, 1) < score && at(0, 1) < score && at(1, 1) < score) {
                        const size_t i = size_t(yis * wMap + xis);
                        maxima.push_back({ xis * sub + x0Range + r + 0.5 + xOffsetMap[i], yis * sub + y0Range + r + 0.5 + yOffsetMap[i],
                                           double(radiusMap[i] * 2), score });
                    }
                }
            // Simulated cameras are perfectly symmetric: then the single best.
            if (maxima.empty()) maxima.push_back({ xBest, yBest, double(rContrastBest * 2), scoreBest });
            // Only those with no better one overlapping.
            std::vector<Circle> filtered;
            const double sqMinDistance = double(maxDiameter) * maxDiameter;
            for (const Circle& cand : maxima) {
                bool alone = true;
                for (const Circle& other : maxima)
                    if (other.score > cand.score) {
                        const double dx = cand.x - other.x, dy = cand.y - other.y;
                        if (dx * dx + dy * dy < sqMinDistance) {
                            alone = false;
                            break;
                        }
                    }
                if (alone) filtered.push_back(cand);
            }
            filtered = sortAndLimit(filtered, q.maxTargetCount * kIterationTargetsFactor, q.corrSymmetry);
            std::vector<Circle> sampled;
            if (finalPass) {
                sampled = filtered;
            } else {
                for (const Circle& best : filtered) {
                    const std::vector<Circle> local = finer(best.x, best.y, 1);
                    if (!local.empty()) sampled.push_back(local.front());
                }
            }
            ret = sortAndLimit(sampled, q.maxTargetCount, q.corrSymmetry);
        }
    } else if (finalPass) {
        if (scoreBest > q.minSymmetry) ret.push_back({ xBest, yBest, double(rContrastBest * 2), scoreBest });
    } else {
        ret = finer(xBest, yBest, 1);
    }

    if (showDiagnostics) {
        const double rscale = 1 / (scoreRange.heat(scoreRange.maxScore) - scoreRange.heat(scoreRange.minScore));
        const double scale = 255 * channels * rscale;
        for (int yi = -sub / 2; yi < hRange - sub / 2; ++yi)
            for (int xi = -sub / 2; xi < wRange - sub / 2; ++xi) {
                const int xis = (xi + sub / 2) / sub, yis = (yi + sub / 2) / sub;
                const double s = scoreMap[size_t(yis * wMap + xis)];
                if (s <= kScoreFloor) continue;
                const int col = xi + x0Range + r, row = yi + y0Range + r;
                const int distance2 = int(std::floor(std::hypot(col - xBest, row - yBest) + 0.5));
                // Not inside the local search range (the finer pass paints there).
                if (sub != 1 && distance2 < sub * kIterationRadius / 2) continue;
                double heat = scoreRange.heat(s);
                if (!std::isfinite(heat)) heat = scoreRange.heat(scoreRange.minScore);
                heat -= scoreRange.heat(scoreRange.minScore);
                const double score = heat * scale;
                // The indicator: the circles found with cross hairs, or the nominal circle dashed.
                double indicate = 0.0;
                if (outermost && q.diagnostics) {
                    if (ret.empty()) {
                        const double dx = col - q.xCenter + 0.501, dy = row - q.yCenter + 0.501;
                        const double nominal = double((maxDiameter + minDiameter) / 4);
                        indicate = 1.0 - std::abs(std::sqrt(dx * dx + dy * dy) - nominal);
                        if (indicate > 0 && ((int(std::atan2(dy, dx) / M_PI * 12 + 12) & 0x1) == 0)) indicate = 0.0;
                    } else {
                        for (const Circle& c : ret) {
                            const double dx = col - c.x + 0.501, dy = row - c.y + 0.501;
                            const double distance = std::sqrt(dx * dx + dy * dy);
                            if (distance < c.diameter) {
                                indicate = 1.0 - std::min(std::min(std::abs(distance - c.diameter / 2), std::abs(dx)), std::abs(dy));
                                if (indicate > 0) break;
                            }
                        }
                    }
                }
                // The score as a heat map, blended over the picture.
                double alpha = std::pow(heat * rscale, 3);
                double alphaCompl = 1 - alpha;
                uchar* px = image.ptr<uchar>(row) + col * channels;
                if (channels == 3) {
                    int red = 0, green = 0, blue = 0;
                    if (indicate > 0) {
                        (ret.empty() ? red : green) = 255;
                        alpha = indicate;
                        alphaCompl = 1 - alpha;
                    } else if (score <= 255) {
                        blue = int(score);
                    } else if (score <= 255 + 255) {
                        blue = int(255 + 255 - score);
                        red = int(score - 255);
                    } else {
                        red = 255;
                        green = int(score - 255 - 255);
                    }
                    if (indicate > 0 || q.heatMap) {
                        px[2] = uchar(int(alpha * red + alphaCompl * px[2]));
                        px[1] = uchar(int(alpha * green + alphaCompl * px[1]));
                        px[0] = uchar(int(alpha * blue + alphaCompl * px[0]));
                    }
                } else {
                    px[0] = indicate > 0 ? 255 : uchar(int(alpha * score + alphaCompl * px[0]));
                }
            }
    }
    return ret;
}

} // inline namespace jf
