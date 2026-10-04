// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// A KiCad footprint's pads read as OpenPnP's KicadModImporter reads them:
// top-copper SMD pads only, Y turned up, shapes to roundness; a pad written
// on one line (older KiCad) or over several (newer) the same.
// Tests check with assert(); a Release build must not compile it away.
#undef NDEBUG
#include <cassert>

#include "model/JPKicadModImporter.h"

#include <cmath>

using namespace jf;

namespace {
bool near(double a, double b) { return std::fabs(a - b) < 1e-9; }
}

int main() {
    // KiCad 5: a pad on one line.
    const char* oneLine = R"((module R_0603 (layer F.Cu)
  (pad 1 smd roundrect (at -0.7875 0) (size 0.875 0.95) (layers F.Cu F.Paste F.Mask) (roundrect_rratio 0.25))
  (pad 2 smd rect (at 0.7875 0.5 90) (size 0.875 0.95) (layers F.Cu F.Paste F.Mask))
  (pad 3 thru_hole circle (at 0 2) (size 1 1) (drill 0.5) (layers *.Cu *.Mask))
  (pad 4 smd rect (at 0 -2) (size 1 1) (layers B.Cu B.Paste B.Mask))
))";
    auto pads = JPKicadModImporter::parse(oneLine);
    assert(pads.size() == 2);
    assert(pads[0].name == "1" && near(pads[0].x, -0.7875) && near(pads[0].y, 0) && near(pads[0].roundness, 25));
    assert(near(pads[0].width, 0.875) && near(pads[0].height, 0.95));
    assert(pads[1].name == "2" && near(pads[1].y, -0.5) && near(pads[1].rotation, 90) && pads[1].roundness == 0);

    // KiCad 8: each pad over several lines, names quoted.
    const char* multiLine = R"((footprint "SOT-23"
	(pad "1" smd roundrect
		(at -0.9375 0.95)
		(size 1.325 0.6)
		(layers "F.Cu" "F.Paste" "F.Mask")
		(roundrect_rratio 0.25)
	)
	(pad "2" smd oval
		(at 0.9375 0)
		(size 1.325 0.6)
		(layers "F.Cu" "F.Paste" "F.Mask")
	)
	(pad "3" smd custom
		(at 0 1)
		(size 1 1)
		(layers "F.Cu")
	)
))";
    pads = JPKicadModImporter::parse(multiLine);
    assert(pads.size() == 2);
    assert(pads[0].name == "1" && near(pads[0].y, -0.95) && near(pads[0].roundness, 25) && near(pads[0].width, 1.325));
    assert(pads[1].name == "2" && pads[1].roundness == 100);
    return 0;
}
