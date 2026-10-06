// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// OpenPnP's CalculateBoardLocationTests, all sixteen: a placement's place on the machine from its board's location
// (top side R6, bottom side R17), with the board's width or without, with the placement transform a simulated
// fiducial check derives (three fiducials, the bottom side mirrored for the fit and back) or without; and the
// inverse, the machine place back onto the board.
// Tests check with assert(); a Release build must not compile it away.
#undef NDEBUG
#include <cassert>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>

#include "model/JPAffineTransform.h"
#include "model/JPBoard.h"
#include "model/JPBoardLocation.h"
#include "model/JPFiducialFit.h"

using namespace jf;

namespace {

JPLocation mm(double x, double y, double z, double c) { return JPLocation(JPLengthUnit::Millimeters, x, y, z, c); }

// OpenPnP's Utils2D.normalizeAngle: 0..360.
double normalizeAngle(double a) {
    a = std::fmod(a, 360);
    return a < 0 ? a + 360 : a;
}

void within(const std::string& test, const char* name, double value, double target, double plusMinus) {
    if (value > target + plusMinus || value < target - plusMinus) {
        std::fprintf(stderr, "%s: %s %g is not within %g of %g\n", test.c_str(), name, value, plusMinus, target);
        assert(false);
    }
}

// OpenPnP's Utils2DTest.checkNormalized.
void checkNormalized(const std::string& test, const JPLocation& r, double x, double y, double z, double c) {
    within(test, "angle", normalizeAngle(r.rotation()), normalizeAngle(c), 0.001);
    within(test, "x", r.x(), x, 0.01);
    within(test, "y", r.y(), y, 0.01);
    within(test, "z", r.z(), z, 0.01);
}

struct TestBoard {
    JPBoardLocation location;
    JPLocation      p1 { JPLengthUnit::Millimeters };
};

// OpenPnP's createTestBoardLocation.
std::unique_ptr<TestBoard> createTestBoardLocation(JPSide side, bool includeBoardWidth) {
    auto t = std::make_unique<TestBoard>();
    auto board = std::make_shared<JPBoard>();
    if (includeBoardWidth) board->dimensions = mm(37.0, 0, 0, 0);
    t->location.holder = board;
    t->location.setGlobalSide(side);
    if (side == JPSide::Top) {
        t->p1 = mm(25, 22, 0, 45);   // R6
        t->location.setLocation(mm(37.746, 100.964, -10, 74.628));
    } else {
        t->p1 = mm(12, 22, 0, 45);   // R17
        t->location.setLocation(includeBoardWidth ? mm(84.107, 46.671, -10, 36.662) : mm(113.763, 68.740, -10, 36.585));
    }
    return t;
}

// OpenPnP's simulateFiducialCheck: the placement transform three fiducials would give, set.
void simulateFiducialCheck(JPBoardLocation& l) {
    JPLocation fid1 = mm(1, 1, 0, 0), fid2 = mm(2, 1, 0, 0), fid3 = mm(2, 2, 0, 0);
    l.setLocalToParentTransform(std::nullopt);
    const std::vector<JPLocation> global { l.placementLocation(fid1), l.placementLocation(fid2), l.placementLocation(fid3) };
    // The bottom side's coordinates are left-handed: X negated for the fit, the fit's X scale negated after.
    const bool bottom = l.globalSide() == JPSide::Bottom;
    if (bottom) {
        fid1 = fid1.multiply(-1, 1, 1, 1);
        fid2 = fid2.multiply(-1, 1, 1, 1);
        fid3 = fid3.multiply(-1, 1, 1, 1);
    }
    JPAffineTransform tx = JPFiducialFit::derive({ fid1, fid2, fid3 }, global);
    if (bottom) tx.scale(-1, 1);
    l.setLocalToParentTransform(tx);
}

void forward(const std::string& name, JPSide side, bool affine, bool width, double x, double y, double z, double c) {
    auto t = createTestBoardLocation(side, width);
    if (affine) {
        simulateFiducialCheck(t->location);
        const JPAffineTransform tx = t->location.localToParentTransform();
        t->location.setLocation(mm(0, 0, -10, 0));
        t->location.setLocalToParentTransform(tx);
    }
    checkNormalized(name, t->location.placementLocation(t->p1), x, y, z, c);
}

void inverse(const std::string& name, JPSide side, bool affine, bool width) {
    auto t = createTestBoardLocation(side, width);
    if (affine) {
        simulateFiducialCheck(t->location);
        const JPAffineTransform tx = t->location.localToParentTransform();
        t->location.setLocation(mm(0, 0, -10, 0));
        t->location.setLocalToParentTransform(tx);
    }
    const JPLocation p1l = t->location.placementLocation(t->p1);
    const JPLocation p1li = t->location.placementLocationInverse(p1l);
    checkNormalized(name, t->p1, p1li.x(), p1li.y(), p1li.z(), p1li.rotation());
}

} // namespace

int main() {
    const JPSide top = JPSide::Top, bottom = JPSide::Bottom;
    forward("calculateBoardLocationTopNoAffineNoWidth", top, false, false, 23.160, 130.902, -10.00, 119.628);
    forward("calculateBoardLocationTopWithAffineNoWidth", top, true, false, 23.160, 130.902, -10.00, 119.628);
    forward("calculateBoardLocationTopNoAffineWithWidth", top, false, true, 23.160, 130.902, -10.00, 119.628);
    forward("calculateBoardLocationTopWithAffineWithWidth", top, true, true, 23.160, 130.902, -10.00, 119.628);
    forward("calculateBoardLocationBottomNoAffineNoWidth", bottom, false, false, 91.015, 79.253, -10.00, 81.585);
    forward("calculateBoardLocationBottomWithAffineNoWidth", bottom, true, false, 91.015, 79.253, -10.00, 81.585);
    forward("calculateBoardLocationBottomNoAffineWithWidth", bottom, false, true, 91.025, 79.246, -10.00, 81.662);
    forward("calculateBoardLocationBottomWithAffineWithWidth", bottom, true, true, 91.025, 79.246, -10.00, 81.662);
    for (const JPSide side : { top, bottom })
        for (const bool affine : { false, true })
            for (const bool width : { false, true })
                inverse(std::string("calculateBoardLocationInverse") + (side == top ? "Top" : "Bottom") + (affine ? "WithAffine" : "NoAffine")
                            + (width ? "WithWidth" : "NoWidth"),
                        side, affine, width);
    return 0;
}
