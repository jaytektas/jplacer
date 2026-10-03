// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include <j/core/SceneGraph.h>
#include <j/graphics/VectorGraphics.h>

#include <functional>
#include <string>
#include <vector>

inline namespace jf {

// What is drawn over a camera's picture to measure by (right-click the
// picture): nothing, the cross through the point the camera looks at, or with
// the cross a grid, a ruler along the machine's axes, or a circle or square of
// a size, all in millimetres on the machine. Those in millimetres are drawn
// through the camera's calibration, along the machine's axes however the
// camera is turned, and bent as the lens bends the picture; a camera not
// calibrated shows only the cross.
struct JPReticle {
    enum class Kind { None, Cross, Grid, Ruler, Circle, Square };

    Kind   kind      = Kind::Cross;
    double spacingMm = 1.0;   // between the Grid's lines, the Ruler's marks
    double sizeMm    = 1.0;   // the Circle's diameter, the Square's side

    // Every kind, in the order the menu lists them, and what it calls them.
    static const std::vector<Kind>& kinds();
    static std::string name(Kind kind);
    // The spacings and sizes to choose from.
    static const std::vector<double>& spacings();
    static const std::vector<double>& sizes();
    // Drawn in millimetres, so only through a calibration.
    bool toScale() const { return kind != Kind::None && kind != Kind::Cross; }

    // As kept in the settings ("grid 1 1"), and read back; unknown text is
    // the cross.
    std::string toText() const;
    static JPReticle fromText(const std::string& text);

    // Where on screen a point so far from what the camera looks at is (mm
    // along the machine's X and Y); false where the picture does not show it.
    using Place = std::function<bool(double xMm, double yMm, float& x, float& y)>;
    // Drawn into `vg`: the cross through (cx, cy) across `area` (the picture
    // on screen), then, given `place`, what is in millimetres. `pxPerMm`:
    // about how many screen pixels a millimetre is, so lines closer than `gap`
    // are thinned out; `reachMm`: how far from its middle the picture reaches;
    // `tick`: a ruler mark's length on screen.
    void draw(JVectorCanvas& vg, const JRect& area, float cx, float cy, const Place& place, double pxPerMm,
              double reachMm, float line, float tick, float gap, const JPaint& paint) const;

private:
    // A line in millimetres from a to b, bent as the picture shows it.
    static void line(JVectorCanvas& vg, const Place& place, double ax, double ay, double bx, double by, float width,
                     const JPaint& paint);
};

} // inline namespace jf
