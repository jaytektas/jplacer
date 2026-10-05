// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPWhiteBalance.h"

#include <algorithm>
#include <cmath>

inline namespace jf {

namespace {

// OpenPnP's fractiles: the lead (where the signal is measured), the gamma point.
constexpr double kLeadFractile  = 0.8;
constexpr double kGammaFractile = 0.5;
// Below this the picture is too dark to balance (OpenPnP: "exposure is too low").
constexpr double kLeastLead = 32;

} // namespace

JPWhiteBalance::JPWhiteBalance(const Values& v) : m_neutral(v.neutral()) {
    if (v.mapped()) {
        // OpenPnP's mapped table: each level's map, interpolated between the levels either side (extrapolated at the ends).
        const int levels = int(v.maps[0].size()), range = 256 / levels, halfRange = range / 2;
        for (int i = 0; i < 256; ++i) {
            int level1 = (i + halfRange) / range, level0 = level1 - 1;
            double weight1 = ((i + halfRange) % range) / double(range), weight0 = 1.0 - weight1;
            if (level0 < 0) {
                level0 = 0;
                weight0 = -weight0;
            }
            if (level1 >= levels) {
                level1 = levels - 2;
                weight0 += 2 * weight1;
                weight1 = -weight1;
            }
            for (size_t ch = 0; ch < 3; ++ch) {
                const double out = (v.maps[ch][size_t(std::max(0, level0))] * weight0 + v.maps[ch][size_t(std::max(0, level1))] * weight1) * 255.0;
                m_table[ch][size_t(i)] = uint8_t(std::clamp(out, 0.0, 255.0));
            }
        }
        return;
    }
    for (size_t ch = 0; ch < 3; ++ch)
        for (int i = 0; i < 256; ++i) {
            const double g = v.gamma[ch] > 0 ? v.gamma[ch] : 1;
            const double out = std::pow(std::max(0.0, i / 255.0 * v.balance[ch]), 1 / g) * 255.0;
            m_table[ch][size_t(i)] = uint8_t(std::clamp(out, 0.0, 255.0));
        }
}

void JPWhiteBalance::apply(JPFrame& frame) const {
    if (m_neutral) return;
    uint8_t* p = frame.rgba.data();
    for (size_t i = 0, n = frame.rgba.size() / 4; i < n; ++i, p += 4) {
        p[0] = m_table[0][p[0]];
        p[1] = m_table[1][p[1]];
        p[2] = m_table[2][p[2]];
    }
}

std::optional<JPWhiteBalance::Values> JPWhiteBalance::automatic(const JPFrame& frame, bool averaged, std::string& why) {
    std::array<std::array<double, 256>, 3> histogram{};
    const uint8_t* p = frame.rgba.data();
    const size_t pixels = frame.rgba.size() / 4;
    for (size_t i = 0; i < pixels; ++i, p += 4)
        for (size_t ch = 0; ch < 3; ++ch) histogram[ch][p[ch]] += 1;
    std::array<double, 3> gammaAt{}, leadAt{}, mean{};
    for (size_t ch = 0; ch < 3; ++ch) {
        double accumulated = 0, sum = 0, n = 0;
        for (int bin = 0; bin < 256; ++bin) {
            const double value = histogram[ch][size_t(bin)];
            accumulated += value;
            if (accumulated < double(pixels) * kGammaFractile) gammaAt[ch] = bin;
            if (accumulated < double(pixels) * kLeadFractile) leadAt[ch] = bin;
            else {
                sum += bin * value;
                n += value;
            }
        }
        mean[ch] = n > 0 ? sum / n : 0;
    }
    const std::array<double, 3>& result = averaged ? mean : leadAt;
    const double lead = std::max({ result[0], result[1], result[2] });
    if (lead < kLeastLead || result[0] <= 0 || result[1] <= 0 || result[2] <= 0) {
        why = "the picture is too dark to balance: give the camera more light or exposure";
        return std::nullopt;
    }
    Values v;
    for (size_t ch = 0; ch < 3; ++ch) {
        v.balance[ch] = lead / result[ch];
        gammaAt[ch] *= v.balance[ch] / 255;
    }
    const double mid = (gammaAt[0] + gammaAt[1] + gammaAt[2]) / 3;
    for (size_t ch = 0; ch < 3; ++ch)
        v.gamma[ch] = gammaAt[ch] > 0 && mid > 0 && mid != 1 ? std::log(gammaAt[ch]) / std::log(mid) : 1;
    return v;
}

std::optional<JPWhiteBalance::Values> JPWhiteBalance::automaticMapped(const JPFrame& frame, int levels, std::string& why) {
    // OpenPnP's autoAdjustWhiteBalanceMapped: a gray gradient in view; at each level of each channel,
    // what the gray's brightness (amplified to the brightest channel's lead) is, its median.
    const int range = 256 / levels, halfRange = range / 2;
    constexpr int kClip = 254, kBalanceLimit = 4;
    const uint8_t* p = frame.rgba.data();
    const size_t pixels = frame.rgba.size() / 4;
    std::array<std::array<double, 256>, 3> histogram{};
    for (size_t i = 0; i < pixels; ++i, p += 4)
        for (size_t ch = 0; ch < 3; ++ch) histogram[ch][p[ch]] += 1;
    std::array<double, 3> lead{};
    int fractileLead = 0;
    for (size_t ch = 0; ch < 3; ++ch) {
        double accumulated = 0;
        for (int bin = 0; bin < 256; ++bin) {
            accumulated += histogram[ch][size_t(bin)];
            if (accumulated > double(pixels) * kLeadFractile) {
                lead[ch] = bin;
                fractileLead = std::max(fractileLead, bin);
                break;
            }
        }
    }
    if (lead[0] + lead[1] + lead[2] <= 0) {
        why = "the picture is too dark to balance: give the camera more light or exposure";
        return std::nullopt;
    }
    const double amplify = fractileLead * 3.0 / (lead[0] + lead[1] + lead[2]);
    std::vector<std::vector<std::vector<long>>> atLevel(3, std::vector<std::vector<long>>(size_t(levels), std::vector<long>(size_t(255 + range), 0)));
    std::vector<std::vector<long>> counts(3, std::vector<long>(size_t(levels), 0));
    p = frame.rgba.data();
    for (size_t i = 0; i < pixels; ++i, p += 4) {
        const int rgb[3] = { p[0], p[1], p[2] };
        const int lum = (rgb[0] >= kClip || rgb[1] >= kClip || rgb[2] >= kClip) ? 255 : int(amplify * (rgb[0] + rgb[1] + rgb[2]) / 3);
        if (lum < 1) continue;
        for (size_t ch = 0; ch < 3; ++ch) {
            const int c = rgb[ch];
            if (c < 1 || lum / c >= kBalanceLimit || c / lum >= kBalanceLimit) continue;
            const int level = std::min(levels - 1, c / range), mid = level * range + halfRange;
            atLevel[ch][size_t(level)][size_t(std::min(255, mid * lum / c))]++;
            counts[ch][size_t(level)]++;
        }
    }
    Values v;
    for (size_t ch = 0; ch < 3; ++ch) {
        std::vector<double>& map = v.maps[ch];
        map.assign(size_t(levels), 0);
        for (int level = 0; level < levels; ++level) {
            const long median = counts[ch][size_t(level)] / 2;
            long accumulated = 0;
            for (int bin = 0; bin < 256; ++bin) {
                accumulated += atLevel[ch][size_t(level)][size_t(bin)];
                if (accumulated > median) {
                    map[size_t(level)] = bin / 255.0;
                    break;
                }
            }
        }
        // A level out of order: the last extrapolated, one between interpolated, or no gray at it.
        for (int level = 1; level < levels; ++level) {
            if (map[size_t(level - 1)] <= map[size_t(level)]) continue;
            if (level + 1 >= levels) {
                map[size_t(level)] = level >= 2 ? (map[size_t(level - 1)] * 3 - map[size_t(level - 2)]) / 2 : map[size_t(level - 1)];
            } else if (map[size_t(level - 1)] > map[size_t(level + 1)]) {
                why = "Image does not contain grayscales at level " + std::to_string(100 * level / levels) + " ... "
                    + std::to_string(100 * (level + 1) / levels) + "%.";
                return std::nullopt;
            } else {
                map[size_t(level)] = (map[size_t(level - 1)] + map[size_t(level + 1)]) / 2;
            }
        }
    }
    // The parametric balance as an approximation (feedback, and a start for a manual override).
    std::string ignored;
    if (const auto approx = automatic(frame, false, ignored)) {
        v.balance = approx->balance;
        v.gamma = approx->gamma;
    }
    return v;
}

} // inline namespace jf
