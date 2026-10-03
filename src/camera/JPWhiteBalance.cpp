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

} // inline namespace jf
