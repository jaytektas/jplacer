// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// A strip's sprocket holes among a pipeline's circles, as OpenPnP's Auto
// Setup finds them: the line of holes a pitch apart beside the part (not the
// circles in the part, nor a stray one), each moved onto the line a whole
// pitch from the next, nearest the part first; then, from two parts clicked
// and the holes by each, the reference and last holes, and the tape the
// wrong way round said so.
// Tests check with assert(); a Release build must not compile it away.
#undef NDEBUG
#include <cassert>

#include "tasks/JPStripHoles.h"

#include <cmath>

using namespace jf;

int main() {
    // 10 px/mm, the camera's centre at (300, 200) over a part; the holes 4.5 mm off it, 4 mm apart, slightly off.
    std::vector<JPStripHoles::Circle> circles;
    for (int i = -3; i <= 3; ++i) circles.push_back({ 300 + i * 40.0 + (i % 2 ? 0.6 : -0.4), 155 + (i % 2 ? 0.3 : -0.2), 15 });
    circles.push_back({ 302, 201, 12 });   // in the part
    circles.push_back({ 298, 199, 12 });
    circles.push_back({ 470, 260, 15 });   // a stray
    const JPStripHoles::Result r = JPStripHoles::find(circles, { 300, 200 }, 10, 8);
    assert(r.hasBest && !r.lines.empty());
    assert(r.inLine.size() == 7);
    // Nearest the part first, a whole pitch apart on one line.
    assert(std::abs(r.inLine[0].x - 300) < 1 && std::abs(r.inLine[0].y - 155) < 1);
    for (const JPStripHoles::Circle& c : r.inLine) {
        const double steps = (c.x - r.inLine[0].x) / 40;
        assert(std::abs(steps - std::round(steps)) < 1e-6 && std::abs(c.y - r.inLine[0].y) < 0.5);
    }
    // One hole not found (a faint one, as clear tape's): OpenPnP's unbroken run finds no line at all; the holes
    // either side of the gap are taken, still a whole pitch apart. As on the bench: marks at 212, 312, 513, 616.
    {
        std::vector<JPStripHoles::Circle> gap { { 565.5, 212.5, 42 }, { 563.5, 513.5, 44 }, { 562.5, 616.5, 46 }, { 564.5, 312.5, 42 } };
        const JPStripHoles::Result g = JPStripHoles::find(gap, { 640, 360 }, 25.2, 8);
        assert(g.hasBest && g.inLine.size() == 4);
        for (const JPStripHoles::Circle& c : g.inLine) {
            const double steps = std::hypot(c.x - g.inLine[0].x, c.y - g.inLine[0].y) / (4 * 25.2);
            assert(std::abs(steps - std::round(steps)) < 1e-6);
        }
        // Only scattered marks, none side by side: still none.
        assert(!JPStripHoles::find({ { 565, 212, 42 }, { 565, 414, 42 }, { 565, 616, 42 } }, { 640, 360 }, 25.2, 8).hasBest);
    }
    // No line of holes beside it: none.
    assert(!JPStripHoles::find({ { 300, 200, 12 }, { 302, 201, 12 } }, { 300, 200 }, 10, 8).hasBest);

    // Parts at (0, 0) and (4, 0) mm, fed along +X; the holes on the right, 3.5 mm off.
    using L = JPLocation;
    const auto mm = JPLengthUnit::Millimeters;
    JPLocation ref1(mm), ref2(mm);
    std::string why;
    assert(JPStripHoles::referenceHoles(L(mm, 0, 0, 0, 0), L(mm, 4, 0, 0, 0), { L(mm, 2, -3.5, 0, 0), L(mm, -2, -3.5, 0, 0) },
                                        { L(mm, 2, -3.5, 0, 0), L(mm, 6, -3.5, 0, 0) }, ref1, ref2, why));
    assert(ref2.x() == 6 && ref1.x() == 2);
    // 2 mm pitched parts sharing the hole: part 1's other.
    assert(JPStripHoles::referenceHoles(L(mm, 0, 0, 0, 0), L(mm, 2, 0, 0, 0), { L(mm, 2, -3.5, 0, 0), L(mm, -2, -3.5, 0, 0) },
                                        { L(mm, 2, -3.5, 0, 0), L(mm, -2, -3.5, 0, 0) }, ref1, ref2, why));
    assert(ref2.x() == 2 && ref1.x() == -2);
    // The holes on the left: the tape the wrong way round.
    assert(!JPStripHoles::referenceHoles(L(mm, 0, 0, 0, 0), L(mm, 4, 0, 0, 0), { L(mm, 2, 3.5, 0, 0) }, { L(mm, 6, 3.5, 0, 0) }, ref1,
                                         ref2, why));
    assert(why == "The tape is oriented incorrectly for the feed direction of the components selected");
    return 0;
}
