// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// Bottom vision's part mask for a part seen whole (JPVisionPipelinePrep::shot): as big as the part reaches (its
// pads, its body) and how far off the nozzle it may be picked, not the tip's Max Part Diameter: an 0402 (1 x 0.5 mm)
// on a tip for parts up to 5 mm is masked to about 2.1 mm, so nothing bright round the tip is boxed with it. A body
// reaching past its pads is masked to the body; never past the tip's own mask.
// Tests check with assert(); a Release build must not compile it away.
#undef NDEBUG
#include <cassert>

#include "pipeline/JPPipeline.h"
#include "tasks/JPVisionComposite.h"
#include "tasks/JPVisionPipelinePrep.h"

#include <cmath>

using namespace jf;

namespace {

double maskMm(const JPFootprint& f, double maxPartDiameterMm, double toleranceMm) {
    JPVisionComposite::Input in;
    in.footprint = f;
    in.toleranceMm = toleranceMm;
    in.maxPartDiameterMm = maxPartDiameterMm;
    in.cameraWidthMm = in.cameraHeightMm = 20;
    const JPVisionComposite c(in);
    JPPipeline p;
    p.context().pixelsPerMmX = p.context().pixelsPerMmY = 50;
    JPNozzleTipConfig tip;
    tip.maxPickToleranceMm = toleranceMm;
    JPVisionPipelinePrep::shot(p, c, c.shots().front(), &tip, 640, 360);
    const JPPipelineValue* v = p.property("partmask.diameter");
    assert(v);
    return std::get<JPPipelineValue::LengthMm>(v->value).mm;
}

bool near(double a, double b) { return std::abs(a - b) < 1e-6; }

} // namespace

int main() {
    // An 0402: pads 0.5 x 0.5 at +-0.25 mm (to 0.5 mm out), its body 1 x 0.5.
    JPFootprint r0402;
    r0402.bodyWidth = 1.0;
    r0402.bodyHeight = 0.5;
    r0402.pads.push_back({ "1", -0.25, 0, 0.5, 0.5 });
    r0402.pads.push_back({ "2", 0.25, 0, 0.5, 0.5 });
    const double reach = std::hypot(0.5, 0.25);   // a pad's outer corner (and the body's)
    assert(near(maskMm(r0402, 5.0, 0.5), 2 * (reach + 0.5)));
    // A body past its pads: masked to the body's corners.
    JPFootprint can = r0402;
    can.bodyWidth = can.bodyHeight = 3.0;
    assert(near(maskMm(can, 5.0, 0.5), 2 * (std::hypot(1.5, 1.5) + 0.5)));
    // Never past the tip's own mask (its Max Part Diameter, with the tolerance).
    assert(maskMm(can, 2.0, 0.5) <= 2.0 + 2 * 0.5 + 1e-9);
    return 0;
}
