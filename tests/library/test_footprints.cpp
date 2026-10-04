// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// Footprints made from a package's numbers sit at IPC-7351's zero (pin 1
// upper left, counted down the left and anticlockwise round a quad); the
// starter library's packages each have a footprint and are found by the
// names KiCad, EasyEDA and suppliers give them.
// Tests check with assert(); a Release build must not compile it away.
#undef NDEBUG
#include <cassert>

#include "library/JPFootprintMaker.h"
#include "library/JPStarterLibrary.h"

#include <cmath>
#include <set>

using namespace jf;

namespace {

bool near(double a, double b) { return std::abs(a - b) < 1e-9; }

} // namespace

int main() {
    // SOIC-8: 1..4 down the left from the top, 5..8 up the right.
    {
        JPFootprintMaker::Dual d;
        const JPFootprint f = JPFootprintMaker::dual("SOIC-8", d);
        assert(f.pads.size() == 8 && f.pin1 == "1");
        const JPPad* p1 = f.pad("1");
        const JPPad* p4 = f.pad("4");
        const JPPad* p5 = f.pad("5");
        const JPPad* p8 = f.pad("8");
        assert(p1->x < 0 && near(p1->y, 1.905) && near(p4->y, -1.905) && p4->x < 0);
        assert(p5->x > 0 && near(p5->y, -1.905) && near(p8->y, 1.905));
        assert(near(p1->x, -2.475) && near(p1->width, 1.95) && near(p1->height, 0.6));
    }
    // A quad: anticlockwise from the top of the left side, with an exposed pad.
    {
        JPFootprintMaker::Quad q;
        q.pinsPerSide = 4;
        q.exposedPad = 1.7;
        const JPFootprint f = JPFootprintMaker::quad("QFN-16", q);
        assert(f.pads.size() == 17);
        assert(f.pad("1")->x < 0 && f.pad("1")->y > 0);    // left, top
        assert(f.pad("5")->y < 0 && f.pad("5")->x < 0);    // bottom, left
        assert(f.pad("9")->x > 0 && f.pad("9")->y < 0);    // right, bottom
        assert(f.pad("13")->y > 0 && f.pad("13")->x > 0);  // top, right
        assert(near(f.pad("5")->width, q.padWidth) && near(f.pad("5")->height, q.padLength));   // turned
        assert(f.pad("17")->x == 0 && near(f.pad("17")->width, 1.7));
    }
    // The starter library.
    {
        JPPartsStore s;
        JPStarterLibrary::fill(s);
        assert(s.parts.empty() && !s.packages.empty());
        std::set<std::string> names;
        for (const JPPackage& k : s.packages) {
            const JPFootprint* f = s.footprint(k.footprintId);
            assert(f && !f->pads.empty() && f->pin1Pad());
            assert(k.height > 0 && k.hasName(k.name));
            for (const std::string& n : k.names) assert(names.insert(n).second);   // each name one package
        }
        auto pads = [&s](const char* name) {
            const JPPackage* k = s.packageNamed(name);
            assert(k);
            return s.footprint(k->footprintId)->pads.size();
        };
        assert(s.packageNamed("R_0603_1608Metric")->name == "0603" && s.packageNamed("C0603")->name == "0603");
        assert(s.packageNamed("0402")->name == "0402" && s.packageNamed("R01005")->name == "01005");
        assert(pads("0805") == 2 && pads("SOT-23") == 3 && pads("SOT-23-5") == 5 && pads("SOT-23-6") == 6);
        assert(pads("SOT-223") == 4 && pads("SOD-123") == 2 && pads("DO-214AC") == 2);
        assert(pads("SOIC-8_3.9x4.9mm_P1.27mm") == 8 && pads("SOIC-28W") == 28 && pads("TSSOP-20_4.4x6.5mm_P0.65mm") == 20);
        assert(pads("QFN-32-1EP_5x5mm_P0.5mm_EP3.45x3.45mm") == 33 && pads("LQFP-64_10x10mm_P0.5mm") == 64);
        // SOT-23: pins 1 and 2 on the left, 3 on the right.
        const JPFootprint* sot = s.footprint(s.packageNamed("SOT-23")->footprintId);
        assert(sot->pad("1")->x < 0 && sot->pad("1")->y > 0 && sot->pad("2")->x < 0 && sot->pad("3")->x > 0);
    }
    return 0;
}
