// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPSimpleHistogram.h"

#include <cmath>

inline namespace jf {

void JPSimpleHistogram::add(double key, double value) {
    // The three bins about the key, weighted by their overlap with a kernel two bins wide.
    // Java's Math.round: halves up.
    const int b1 = int(std::floor(key / m_resolution + 0.5)), b0 = b1 - 1, b2 = b1 + 1;
    const double w0 = ((b0 + 1.5) * m_resolution - key) / m_resolution;
    const double w2 = (-(b2 - 1.5) * m_resolution + key) / m_resolution;
    addBin(b1, value);
    addBin(b0, w0 * value);
    addBin(b2, w2 * value);
}

void JPSimpleHistogram::addBin(int bin, double value) {
    const double now = (m_bins[bin] += value);
    if (!m_maximum || m_maximumValue < now) {
        m_maximumValue = now;
        m_maximum = bin;
    }
    if (!m_minimum || m_minimumValue > now) {
        m_minimumValue = now;
        m_minimum = bin;
    }
}

double JPSimpleHistogram::maximumKey() const { return m_maximum ? *m_maximum * m_resolution : NAN; }

double JPSimpleHistogram::minimumKey() const { return m_minimum ? *m_minimum * m_resolution : NAN; }

} // inline namespace jf
