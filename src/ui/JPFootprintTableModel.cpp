// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPFootprintTableModel.h"

#include "JPLengthCell.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

inline namespace jf {

namespace {

enum Col { kName, kMark, kX, kY, kWidth, kLength, kRotation, kRound };
constexpr const char* kFormat = "%.3f";   // OpenPnP's length display format

std::string formatted(double v) {
    char buf[64];
    std::snprintf(buf, sizeof buf, kFormat, v);
    return buf;
}

bool parseDouble(const std::string& t, double& out) {
    char* end = nullptr;
    out = std::strtod(t.c_str(), &end);
    return end != t.c_str();
}

} // namespace

JPTableModel::Column JPFootprintTableModel::column(int c) const {
    switch (c) {
        case kName:     return { "Name", "", Kind::Text };
        case kMark:     return { "Mark", "", Kind::Text };
        case kX:        return { "X", "", Kind::Text };
        case kY:        return { "Y", "", Kind::Text };
        case kWidth:    return { "Width", "", Kind::Text };
        case kLength:   return { "Length", "", Kind::Text };
        case kRotation: return { "Rot.", "", Kind::Text };
        case kRound:    return { "% Round", "", Kind::Text };
    }
    return {};
}

std::string JPFootprintTableModel::text(int row, int c) const {
    if (!m_footprint || row < 0 || size_t(row) >= m_footprint->pads.size()) return {};
    const JPFootprint::Pad& p = m_footprint->pads[size_t(row)];
    const JPLengthUnit u = m_footprint->units;
    switch (c) {
        case kName:     return p.name;
        case kMark:     return p.mark ? "O" : "";
        case kX:        return JPLengthCell::text(JPLength(p.x, u), true);
        case kY:        return JPLengthCell::text(JPLength(p.y, u), true);
        case kWidth:    return JPLengthCell::text(JPLength(p.width, u), true);
        case kLength:   return JPLengthCell::text(JPLength(p.height, u), true);
        case kRotation: return formatted(p.rotation);
        case kRound:    return formatted(p.roundness);
    }
    return {};
}

bool JPFootprintTableModel::setText(int row, int c, const std::string& text, std::string& error) {
    if (!m_footprint || row < 0 || size_t(row) >= m_footprint->pads.size()) return false;
    JPFootprint::Pad& p = m_footprint->pads[size_t(row)];
    const JPLengthUnit u = m_footprint->units;
    auto length = [&](double& field) {
        JPLength l;
        if (!JPLengthCell::parse(text, JPLength(field, u), l)) {
            error = "'" + text + "' is not a length";
            return false;
        }
        field = l.convertToUnits(u).value();
        return true;
    };
    double v = 0;
    switch (c) {
        case kName: p.name = text; break;
        case kX: if (!length(p.x)) return false; break;
        case kY: if (!length(p.y)) return false; break;
        case kWidth: if (!length(p.width)) return false; break;
        case kLength: if (!length(p.height)) return false; break;
        case kRotation:
            if (!parseDouble(text, v)) return false;
            p.rotation = v;
            break;
        case kRound:
            if (!parseDouble(text, v)) return false;
            p.roundness = std::clamp(v, -100.0, 100.0);
            break;
        default: return false;
    }
    if (onChanged) onChanged();
    return true;
}

} // inline namespace jf
