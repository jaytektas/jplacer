// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// KiCad footprint files read into footprints: KiCad 6 onward's `footprint`
// and KiCad 5's `module`; pads with their shapes, Y turned over to point up,
// mechanical holes left out, the body from the fabrication outline, pin 1.
// Tests check with assert(); a Release build must not compile it away.
#undef NDEBUG
#include <cassert>

#include "library/JPKicadFootprint.h"

#include <cmath>

using namespace jf;

namespace {

bool near(double a, double b) { return std::abs(a - b) < 1e-9; }

} // namespace

int main() {
    {
        const std::string text = R"((footprint "SOT-23" (version 20221018) (generator pcbnew)
  (layer "F.Cu")
  (descr "SOT, 3 Pin")
  (fp_line (start -0.65 -1.45) (end 0.65 -1.45) (stroke (width 0.1) (type solid)) (layer "F.Fab"))
  (fp_line (start 0.65 1.45) (end -0.65 1.45) (stroke (width 0.1) (type solid)) (layer "F.Fab"))
  (fp_line (start -2 -2) (end 2 2) (stroke (width 0.05) (type solid)) (layer "F.CrtYd"))
  (pad "1" smd roundrect (at -0.9375 -0.95) (size 1.475 0.6) (layers "F.Cu" "F.Paste" "F.Mask") (roundrect_rratio 0.25))
  (pad "2" smd roundrect (at -0.9375 0.95) (size 1.475 0.6) (layers "F.Cu" "F.Paste" "F.Mask") (roundrect_rratio 0.25))
  (pad "3" smd roundrect (at 0.9375 0 90) (size 1.475 0.6) (layers "F.Cu" "F.Paste" "F.Mask") (roundrect_rratio 0.25))
  (pad "" np_thru_hole circle (at 0 0) (size 0.5 0.5) (drill 0.5) (layers "*.Cu" "*.Mask"))
))";
        JPFootprint f;
        std::string error;
        assert(JPKicadFootprint::parse(text, f, error));
        assert(f.name == "SOT-23" && f.pads.size() == 3 && f.pin1 == "1");
        const JPPad* p1 = f.pad("1");
        assert(near(p1->x, -0.9375) && near(p1->y, 0.95));      // KiCad's -0.95 (down is +) is up here
        assert(near(p1->width, 1.475) && near(p1->height, 0.6) && near(p1->roundness, 0.5));
        assert(near(f.pad("3")->rotationDeg, 90));
        assert(near(f.bodyWidth, 1.3) && near(f.bodyLength, 2.9));   // the fabrication outline, not the courtyard
    }
    {
        const std::string text = "(module LED_0603 (layer F.Cu) (tedit 5B301BBE)\n"
                                 "  (fp_line (start -0.8 0.4) (end -0.8 -0.4) (layer F.Fab) (width 0.1))\n"
                                 "  (fp_line (start 0.8 -0.4) (end 0.8 0.4) (layer F.Fab) (width 0.1))\n"
                                 "  (pad 1 smd rect (at -0.7875 0) (size 0.875 0.95) (layers F.Cu F.Paste F.Mask))\n"
                                 "  (pad 2 smd oval (at 0.7875 0) (size 0.875 0.95) (layers F.Cu F.Paste F.Mask))\n"
                                 ")\n";
        JPFootprint f;
        std::string error;
        assert(JPKicadFootprint::parse(text, f, error));
        assert(f.name == "LED_0603" && f.pads.size() == 2 && f.pad("1")->roundness == 0 && f.pad("2")->roundness == 1);
        assert(near(f.bodyWidth, 1.6) && near(f.bodyLength, 0.8));
    }
    {
        JPFootprint f;
        std::string error;
        assert(!JPKicadFootprint::parse("(kicad_pcb (version 4))", f, error) && !error.empty());
        assert(!JPKicadFootprint::parse("(footprint \"X\" (layer \"F.Cu\")", f, error));   // unbalanced
        assert(!JPKicadFootprint::parse("(footprint \"X\")", f, error));                   // no pads
        assert(!JPKicadFootprint::parse("hello", f, error));
    }
    return 0;
}
