// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// OpenPnP's Utils2DTest: testCalculateBoardPlacementLocationSimple (a placement at 55, 5, 0, 90 on a board at
// 5, 15, -8, -6 lies at 60.22, 14.22, -8, 84, with the board's placement transform set to the same move and turn
// or not) and testAngleFromPoint (the angle of a point every 5 degrees round the circle, given back).
// Tests check with assert(); a Release build must not compile it away.
#undef NDEBUG
#include <cassert>
#include <cmath>
#include <cstdio>

#include "model/JPAffineTransform.h"
#include "model/JPBoard.h"
#include "model/JPBoardLocation.h"

using namespace jf;

namespace {

// OpenPnP's within(): the value within plusMinus of the target.
void within(const char* name, double value, double target, double plusMinus) {
    if (value > target + plusMinus || value < target - plusMinus) {
        std::fprintf(stderr, "%s %g is not within %g of %g\n", name, value, plusMinus, target);
        assert(false);
    }
}

void check(const JPLocation& results, double x, double y, double z, double c) {
    within("angle", results.rotation(), c, 0.001);
    within("x", results.x(), x, 0.01);
    within("y", results.y(), y, 0.01);
    within("z", results.z(), z, 0.01);
}

} // namespace

int main() {
    // testCalculateBoardPlacementLocationSimple
    {
        JPBoardLocation boardLocation;
        boardLocation.holder = std::make_shared<JPBoard>();
        boardLocation.setLocation(JPLocation(JPLengthUnit::Millimeters, 5, 15, -8, -6));
        const JPLocation placement(JPLengthUnit::Millimeters, 55, 5, 0, 90);
        const JPLocation before = boardLocation.placementLocation(placement);
        JPAffineTransform tx;
        tx.translate(5, 15);
        tx.rotate(-6 * M_PI / 180);
        boardLocation.setLocalToGlobalTransform(tx);
        const JPLocation after = boardLocation.placementLocation(placement);
        check(before, 60.22, 14.22, -8, 84);
        check(after, 60.22, 14.22, -8, 84);
    }
    // testAngleFromPoint
    for (double angle = 0; angle <= 360; angle += 5) {
        const JPLocation location0(JPLengthUnit::Millimeters);
        const JPLocation location1(JPLengthUnit::Millimeters, std::cos(angle * M_PI / 180), std::sin(angle * M_PI / 180), -10, 0);
        const double determined = location0.angleTo(location1);
        if (1e-9 < std::abs(std::fmod(determined - angle, 360))) {
            std::fprintf(stderr, "given %g degree angled points, but returned %g\n", angle, determined);
            assert(false);
        }
    }
    return 0;
}
