// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include <map>
#include <optional>

inline namespace jf {

// OpenPnP's SimpleHistogram: values counted in bins `resolution` wide, each
// spread over its bin and half into the two beside it; the fullest bin (the
// first to be fullest) and the emptiest.
class JPSimpleHistogram {
public:
    explicit JPSimpleHistogram(double resolution) : m_resolution(resolution) {}
    void add(double key, double value);
    // The fullest bin's key (NaN: none yet), and the emptiest's.
    double maximumKey() const;
    double minimumKey() const;

private:
    void addBin(int bin, double value);

    double                m_resolution;
    std::map<int, double> m_bins;
    std::optional<int>    m_maximum, m_minimum;
    double                m_maximumValue = 0, m_minimumValue = 0;
};

} // inline namespace jf
