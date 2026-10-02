// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// Placing a board and fitting it back from its fiducials: a board turned and
// moved (and seen from its bottom side) is recovered exactly by the rigid fit
// from two points and by the affine fit from three; the affine fit also takes
// up a machine whose axes are not square, which the rigid fit cannot.
// Tests check with assert(); a Release build must not compile it away.
#undef NDEBUG
#include <cassert>

#include "geometry/JPPointFit.h"

#include <cmath>

using namespace jf;

namespace {

std::vector<JPPointFit::Pair> seen(const JPAffine2D& m, std::initializer_list<std::pair<double, double>> pts) {
    std::vector<JPPointFit::Pair> out;
    for (const auto& [x, y] : pts) {
        JPPointFit::Pair p{ x, y, 0, 0 };
        m.apply(x, y, p.toX, p.toY);
        out.push_back(p);
    }
    return out;
}

bool same(const JPAffine2D& a, const JPAffine2D& b, double tol) {
    return std::abs(a.a - b.a) < tol && std::abs(a.b - b.b) < tol && std::abs(a.c - b.c) < tol && std::abs(a.d - b.d) < tol
        && std::abs(a.tx - b.tx) < tol && std::abs(a.ty - b.ty) < tol;
}

} // namespace

int main() {
    // Placing, mirroring, composing, inverting.
    const JPAffine2D turned = JPAffine2D::placed(100, 50, 90);
    double x, y;
    turned.apply(10, 0, x, y);
    assert(std::abs(x - 100) < 1e-12 && std::abs(y - 60) < 1e-12);
    assert(std::abs(turned.rotationDeg() - 90) < 1e-12 && !turned.mirrored());
    const JPAffine2D bottom = JPAffine2D::placed(347.9, 69.3, -0.04).after(JPAffine2D::mirrorX());
    assert(bottom.mirrored() && std::abs(bottom.rotationDeg() + 0.04) < 1e-9);
    const auto inv = bottom.inverse();
    assert(inv);
    double bx, by;
    bottom.apply(12.5, -3, x, y);
    inv->apply(x, y, bx, by);
    assert(std::abs(bx - 12.5) < 1e-9 && std::abs(by + 3) < 1e-9);

    // A bottom side turned and moved: the rigid fit from two fiducials, given
    // only the mirror and a rough turn, finds it exactly.
    const auto two = seen(bottom, { { 137.393, 2.814 }, { 2.791, 18.734 } });
    const auto rigid = JPPointFit::rigid(two, JPAffine2D::placed(0, 0, 3).after(JPAffine2D::mirrorX()));
    assert(rigid && rigid->rms < 1e-9 && same(rigid->map, bottom, 1e-9));
    // One fiducial: moved only, the turn kept from the base.
    const auto one = JPPointFit::rigid({ two[0] }, JPAffine2D::placed(0, 0, -0.04).after(JPAffine2D::mirrorX()));
    assert(one && one->rms < 1e-9 && same(one->map, bottom, 1e-9));

    // A machine whose Y is skewed: affine from three or more is exact; rigid is not.
    JPAffine2D skew;
    skew.b = 0.0025;   // moving in Y carries X along
    const JPAffine2D onSkewed = skew.after(bottom);
    const auto four = seen(onSkewed, { { 137.393, 2.814 }, { 79.078, 40.651 }, { 161.738, 101.969 }, { 2.791, 18.734 } });
    const auto affine = JPPointFit::affine(four);
    assert(affine && affine->rms < 1e-9 && same(affine->map, onSkewed, 1e-9));
    const auto rigidOnSkewed = JPPointFit::rigid(four, JPAffine2D::mirrorX());
    assert(rigidOnSkewed && rigidOnSkewed->rms > 0.05);

    // Not enough, or all in a line.
    assert(!JPPointFit::affine({ four[0], four[1] }));
    assert(!JPPointFit::affine(seen(bottom, { { 0, 0 }, { 1, 1 }, { 2, 2 } })));
    assert(!JPPointFit::rigid({}, JPAffine2D()));
    return 0;
}
