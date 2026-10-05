// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPFootprint.h"

#include "JPLength.h"
#include "JPXmlValues.h"

#include <algorithm>
#include <cmath>

inline namespace jf {

using V = JPXmlValues;

namespace {

// Points along a quarter circle, for a rounded corner.
constexpr int kArcSteps = 6;
// Pin 1's mark: this share of the pad's smaller side across, at most one of
// the footprint's units.
constexpr double kMarkShare = 0.9, kMarkMost = 1.0;
constexpr int kMarkSteps = 16;

// A pad's outline about its centre before it is turned: a rectangle whose
// corners are rounded by `r` (all four, or with `innerOnly` those on its -X
// half, as OpenPnP adds a square right half to a rounded rectangle).
JPFootprint::Outline rounded(double w, double h, double r, bool innerOnly) {
    JPFootprint::Outline pts;
    const double hw = w / 2, hh = h / 2;
    auto corner = [&](double cx, double cy, double a0, bool round, double sx, double sy) {
        if (!round || r <= 0) {
            pts.push_back({ sx, sy });
            return;
        }
        for (int i = 0; i <= kArcSteps; ++i) {
            const double a = a0 + (M_PI / 2) * double(i) / kArcSteps;
            pts.push_back({ cx + r * std::cos(a), cy + r * std::sin(a) });
        }
    };
    corner(hw - r, -hh + r, -M_PI / 2, !innerOnly, hw, -hh);   // bottom right
    corner(hw - r, hh - r, 0, !innerOnly, hw, hh);             // top right
    corner(-hw + r, hh - r, M_PI / 2, true, -hw, hh);          // top left
    corner(-hw + r, -hh + r, M_PI, true, -hw, -hh);            // bottom left
    return pts;
}

} // namespace

std::vector<JPFootprint::Outline> JPFootprint::padOutlines(const Pad& pad) {
    const double a = pad.rotation * M_PI / 180, ca = std::cos(a), sa = std::sin(a);
    auto placed = [&](Outline o) {
        for (Point& p : o) p = { pad.x + p.x * ca - p.y * sa, pad.y + p.x * sa + p.y * ca };
        return o;
    };
    // OpenPnP's rounding: a corner arc as wide as the smaller side times the roundness.
    const double arc = std::min(pad.width, pad.height) * pad.roundness / 100;
    std::vector<Outline> out { placed(rounded(pad.width, pad.height, std::fabs(arc) / 2, arc < 0)) };
    if (pad.mark) {
        const double r = std::min(std::min(pad.width, pad.height) * kMarkShare, kMarkMost) / 2;
        Outline ring;
        for (int i = 0; i < kMarkSteps; ++i) {
            const double t = 2 * M_PI * i / kMarkSteps;
            ring.push_back({ r * std::cos(t), r * std::sin(t) });
        }
        out.push_back(placed(ring));
    }
    return out;
}

std::vector<JPFootprint::Outline> JPFootprint::padsOutlines() const {
    std::vector<Outline> out;
    for (const Pad& p : pads)
        for (Outline& o : padOutlines(p)) out.push_back(std::move(o));
    return out;
}

JPFootprint JPFootprint::inMillimeters() const {
    JPFootprint f = *this;
    const double mm = JPLength(1, units).convertToUnits(JPLengthUnit::Millimeters).value();
    f.units = JPLengthUnit::Millimeters;
    for (double* v : { &f.bodyWidth, &f.bodyHeight, &f.outerDimension, &f.innerDimension, &f.padPitch, &f.padAcross }) *v *= mm;
    for (Pad& p : f.pads) {
        p.x *= mm;
        p.y *= mm;
        p.width *= mm;
        p.height *= mm;
    }
    return f;
}

JPFootprint::Outline JPFootprint::bodyOutline() const {
    Pad body;
    body.width = bodyWidth;
    body.height = bodyHeight;
    return padOutlines(body).front();
}

void JPFootprint::toggleMark(size_t index) {
    if (index >= pads.size()) return;
    if (pads[index].mark) {
        pads[index].mark = false;
        return;
    }
    for (Pad& p : pads) p.mark = false;
    pads[index].mark = true;
}

bool JPFootprint::generate(Generator type, std::string& error) {
    auto add = [this](const std::string& name, double w, double h, double rot, double x, double y) {
        Pad p;
        p.name = name;
        p.width = w;
        p.height = h;
        p.rotation = rot;
        p.x = x;
        p.y = y;
        p.roundness = padRoundness;
        pads.push_back(p);
    };
    switch (type) {
        case Generator::Dual: {
            if (padCount % 2 != 0 || padCount <= 0) {
                error = "For Dual form factor, the pad count must be positive multiples of 2.";
                return false;
            }
            const double padLength = (outerDimension - innerDimension) / 2;
            const double x = (outerDimension + innerDimension) / 4;
            int n = 0;
            for (; n < padCount / 2; ++n)
                add(std::to_string(n + 1), padLength, padAcross, 180, -x, (padCount / 4.0 - n - 0.5) * padPitch);
            for (; n < padCount; ++n)
                add(std::to_string(n + 1), padLength, padAcross, 0, x, (n - padCount * 3 / 4.0 + 0.5) * padPitch);
            if (bodyWidth == 0 && bodyHeight == 0) {
                bodyWidth = innerDimension;
                bodyHeight = padCount / 2 * padPitch;
            }
            return true;
        }
        case Generator::Quad: {
            if (padCount % 4 != 0 || padCount <= 0) {
                error = "For Quad form factor, the pad count must be positive multiples of 4.";
                return false;
            }
            const double padLength = (outerDimension - innerDimension) / 2;
            const double d = (outerDimension + innerDimension) / 4;
            int n = 0;
            for (; n < padCount * 1 / 4; ++n)
                add(std::to_string(n + 1), padLength, padAcross, 180, -d, (padCount / 8.0 - n - 0.5) * padPitch);
            for (; n < padCount * 2 / 4; ++n)
                add(std::to_string(n + 1), padLength, padAcross, -90, (n - padCount * 3 / 8.0 + 0.5) * padPitch, -d);
            for (; n < padCount * 3 / 4; ++n)
                add(std::to_string(n + 1), padLength, padAcross, 0, d, (n - padCount * 5 / 8.0 + 0.5) * padPitch);
            for (; n < padCount * 4 / 4; ++n)
                add(std::to_string(n + 1), padLength, padAcross, 90, (padCount * 7 / 8.0 - n - 0.5) * padPitch, d);
            if (bodyWidth == 0 && bodyHeight == 0) {
                bodyWidth = innerDimension;
                bodyHeight = innerDimension;
            }
            return true;
        }
        case Generator::Bga: {
            const int cols = int(std::sqrt(double(padCount)));
            const int rows = cols;
            if (std::pow(rows, 2) != padCount) {
                error = "For PGA, the pad count must be a square number.";
                return false;
            }
            for (int row = 0; row < rows; ++row)
                for (int col = 0; col < cols; ++col) {
                    const double x = (col - cols / 2.0 + 0.5) * padPitch;
                    const double y = (rows / 2.0 - row - 0.5) * padPitch;
                    if (std::fabs(x) > innerDimension / 2 || std::fabs(y) > innerDimension / 2)
                        add(std::string(1, char('A' + row)) + std::to_string(col), padAcross, padAcross, 0, x, y);
                }
            outerDimension = (rows - 1) * padPitch + padAcross;
            if (bodyWidth == 0 && bodyHeight == 0) {
                bodyWidth = outerDimension + padPitch;
                bodyHeight = outerDimension + padPitch;
            }
            return true;
        }
        case Generator::Kicad:
            error = "KiCad pads are read from a .kicad_mod file";
            return false;
    }
    return false;
}

JPFootprint JPFootprint::fromXml(const JPXmlElement& e) {
    JPFootprint f;
    f.units = V::units(e, "units");
    f.bodyWidth = V::number(e, "body-width");
    f.bodyHeight = V::number(e, "body-height");
    f.outerDimension = V::number(e, "outer-dimension");
    f.innerDimension = V::number(e, "inner-dimension");
    f.padCount = V::integer(e, "pad-count");
    f.padPitch = V::number(e, "pad-pitch");
    f.padAcross = V::number(e, "pad-across");
    f.padRoundness = V::number(e, "pad-roundness");
    for (const JPXmlElement& c : e.children) {
        if (c.name != "pad") continue;
        Pad p;
        p.name = c.attr("name");
        p.x = V::number(c, "x");
        p.y = V::number(c, "y");
        p.width = V::number(c, "width");
        p.height = V::number(c, "height");
        p.rotation = V::number(c, "rotation");
        p.mark = V::boolean(c, "mark");
        p.roundness = V::number(c, "roundness");
        f.pads.push_back(p);
    }
    return f;
}

JPXmlNode JPFootprint::toXml() const {
    JPXmlNode n("footprint");
    n.attr("units", V::units(units))
        .attr("body-width", V::number(bodyWidth))
        .attr("body-height", V::number(bodyHeight))
        .attr("outer-dimension", V::number(outerDimension))
        .attr("inner-dimension", V::number(innerDimension))
        .attr("pad-count", std::to_string(padCount))
        .attr("pad-pitch", V::number(padPitch))
        .attr("pad-across", V::number(padAcross))
        .attr("pad-roundness", V::number(padRoundness));
    for (const Pad& p : pads)
        n.add(JPXmlNode("pad"))
            .attr("name", p.name)
            .attr("x", V::number(p.x))
            .attr("y", V::number(p.y))
            .attr("width", V::number(p.width))
            .attr("height", V::number(p.height))
            .attr("rotation", V::number(p.rotation))
            .attr("mark", V::boolean(p.mark))
            .attr("roundness", V::number(p.roundness));
    return n;
}

} // inline namespace jf
