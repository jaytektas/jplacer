// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPVisionComposite.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>

inline namespace jf {

namespace {

using Pad = JPFootprint::Pad;
using Corner = JPVisionComposite::Corner;
using Solution = JPVisionComposite::Solution;
using ShotConfiguration = JPVisionComposite::ShotConfiguration;
using Method = JPVisionCompositing::Method;

constexpr double kEps = 1e-5;
// The least overlap of a corner's view with the next pad along its edge, as a share of the view's radius (2%, ~7 pixels at 720p/2).
constexpr double kMinCameraRadiusOverlap = 0.02;
const double kInvHypot = 1 / std::hypot(1.0, 1.0);
// The octagonal hull's eight directions.
const double kXHull[8] = { -kInvHypot, 0, +kInvHypot, -1, +1, -kInvHypot, 0, +kInvHypot };
const double kYHull[8] = { -kInvHypot, -1, -kInvHypot, 0, 0, +kInvHypot, +1, +kInvHypot };

// A pad's corners, turned by its rotation about the footprint's origin (as OpenPnP's Pad.corners()).
std::array<JPVisionComposite::Point, 4> padCorners(const Pad& p) {
    std::array<JPVisionComposite::Point, 4> pts { { { p.x - p.width / 2, p.y + p.height / 2 },
                                                    { p.x - p.width / 2, p.y - p.height / 2 },
                                                    { p.x + p.width / 2, p.y - p.height / 2 },
                                                    { p.x + p.width / 2, p.y + p.height / 2 } } };
    const double s = std::sin(p.rotation * M_PI / 180), c = std::cos(p.rotation * M_PI / 180);
    for (auto& pt : pts) pt = { c * pt.x - s * pt.y, s * pt.x + c * pt.y };
    return pts;
}

// The pad as an upright rectangle round it, where it is.
Pad boundingBox(const Pad& p) {
    double x0 = INFINITY, x1 = -INFINITY, y0 = INFINITY, y1 = -INFINITY;
    for (const auto& pt : padCorners(p)) {
        x0 = std::min(pt.x, x0);
        x1 = std::max(pt.x, x1);
        y0 = std::min(pt.y, y0);
        y1 = std::max(pt.y, y1);
    }
    Pad out;
    out.name = p.name;
    out.width = x1 - x0;
    out.height = y1 - y0;
    out.x = p.x;
    out.y = p.y;
    return out;
}

// An edge added unless one is already within kEps; kept in order.
void addCoordinate(std::vector<double>& edges, double c) {
    const auto at = std::lower_bound(edges.begin(), edges.end(), c);
    if (at != edges.end() && std::abs(*at - c) <= kEps) return;
    if (at != edges.begin() && std::abs(*(at - 1) - c) <= kEps) return;
    edges.insert(at, c);
}

double padDistance(double x, double y, const Pad& pad) {
    double dx = 0, dy = 0;
    if (pad.x - pad.width / 2 > x) dx = pad.x - pad.width / 2 - x;
    else if (pad.x + pad.width / 2 < x) dx = pad.x + pad.width / 2 - x;
    if (pad.y - pad.height / 2 > y) dy = pad.y - pad.height / 2 - y;
    else if (pad.y + pad.height / 2 < y) dy = pad.y + pad.height / 2 - y;
    return std::hypot(dx, dy);
}

double xDistance(const Corner& a, const Corner& b) { return std::abs(a.x - b.x); }
double yDistance(const Corner& a, const Corner& b) { return std::abs(a.y - b.y); }

// The corners `c` could be measured with, and the best solution they give (OpenPnP's computeCornerSolution).
std::vector<Corner*> cornerSolution(Corner& c) {
    std::vector<Corner*> solution;
    c.rating = 0;
    size_t minCorners = 0;
    if (c.square) {
        // Both symmetric position and symmetric angle with just two corners.
        minCorners = 2;
        c.rating = 7;
        solution = { &c, c.diagonalBuddy };
        if (c.xMirrorBuddy) {
            solution.push_back(c.xMirrorBuddy);
            c.rating++;
        }
        if (c.yMirrorBuddy) {
            solution.push_back(c.yMirrorBuddy);
            c.rating++;
        }
        c.solution = Solution::Square;
    } else if (c.diagonalBuddy && c.xMirrorBuddy && c.yMirrorBuddy) {
        // Symmetric position and asymmetric angle with three corners, non-square box and X configuration.
        minCorners = 3;
        c.rating = 6;
        solution = { &c, c.diagonalBuddy };
        if (xDistance(c, *c.xMirrorBuddy) > yDistance(c, *c.yMirrorBuddy) - kEps) {
            solution.push_back(c.xMirrorBuddy);
            solution.push_back(c.yMirrorBuddy);
        } else {
            solution.push_back(c.yMirrorBuddy);
            solution.push_back(c.xMirrorBuddy);
        }
        c.solution = Solution::Box;
    } else if (c.diagonalBuddy && (c.xAlignedBuddy || c.yAlignedBuddy)) {
        // Symmetric position and asymmetric angle with three corners.
        minCorners = 3;
        c.rating = 4;
        solution = { &c, c.diagonalBuddy };
        if (!c.yAlignedBuddy) {
            solution.push_back(c.xAlignedBuddy);
            if (c.diagonalBuddy->xAlignedBuddy) {
                solution.push_back(c.diagonalBuddy->xAlignedBuddy);   // Z configuration
                c.rating++;
                c.solution = Solution::Z;
            } else {
                c.solution = Solution::Figure7;
            }
        } else if (!c.xAlignedBuddy) {
            solution.push_back(c.yAlignedBuddy);
            if (c.diagonalBuddy->yAlignedBuddy) {
                solution.push_back(c.diagonalBuddy->yAlignedBuddy);   // Z configuration
                c.rating++;
                c.solution = Solution::Z;
            } else {
                c.solution = Solution::Figure7;
            }
        } else if (yDistance(c, *c.xAlignedBuddy) > xDistance(c, *c.yAlignedBuddy) - kEps) {
            solution.push_back(c.xAlignedBuddy);
            c.rating++;
            if (c.diagonalBuddy->xAlignedBuddy
                && xDistance(c, *c.yAlignedBuddy) - kEps
                       < yDistance(*c.diagonalBuddy, *c.diagonalBuddy->xAlignedBuddy) - kEps) {
                solution.push_back(c.diagonalBuddy->xAlignedBuddy);   // Z configuration
                c.solution = Solution::Z;
            } else {
                solution.push_back(c.yAlignedBuddy);
                c.solution = Solution::Arrow;
            }
        } else {
            solution.push_back(c.yAlignedBuddy);
            c.rating++;
            if (c.diagonalBuddy->yAlignedBuddy
                && xDistance(c, *c.xAlignedBuddy) - kEps
                       < xDistance(*c.diagonalBuddy, *c.diagonalBuddy->yAlignedBuddy) - kEps) {
                solution.push_back(c.diagonalBuddy->yAlignedBuddy);   // Z configuration
                c.solution = Solution::Z;
            } else {
                solution.push_back(c.xAlignedBuddy);
                c.solution = Solution::Arrow;
            }
        }
    } else if (c.xMirrorBuddy && c.yMirrorBuddy) {
        // Asymmetric position and asymmetric angle with three corners.
        minCorners = 3;
        c.rating = 3;
        solution = { &c, c.xMirrorBuddy, c.yMirrorBuddy };
        c.solution = Solution::Angle;
    } else if (c.xMirrorBuddy && c.ySymmetricBuddy) {
        // Trapezoidal position and angle with four corners.
        minCorners = 4;
        c.rating = 2;
        solution = { &c, c.xMirrorBuddy, c.ySymmetricBuddy, c.ySymmetricBuddy->xMirrorBuddy };
        c.solution = Solution::Trapezoid;
    } else if (c.yMirrorBuddy && c.xSymmetricBuddy) {
        minCorners = 4;
        c.rating = 2;
        solution = { &c, c.yMirrorBuddy, c.xSymmetricBuddy, c.xSymmetricBuddy->yMirrorBuddy };
        c.solution = Solution::Trapezoid;
    }
    size_t i = 0;
    for (Corner* corner : solution) corner->optional = ++i > minCorners;
    return solution;
}

// Each other corner `c` aligns, mirrors or is symmetric with; on the second pass, its solution (OpenPnP's computeBuddies).
bool computeCornerBuddies(Corner& c, const std::vector<std::unique_ptr<Corner>>& corners, double minAlignDistance,
                          double minSymmetryDistance, int pass) {
    for (const auto& other : corners) {
        Corner& c2 = *other;
        if (&c == &c2) continue;
        const bool xSymmetric = c.xSign == -c2.xSign && std::abs(c.x + c2.x) < kEps && std::abs(c.x - c2.x) > minSymmetryDistance;
        const bool ySymmetric = c.ySign == -c2.ySign && std::abs(c.y + c2.y) < kEps && std::abs(c.y - c2.y) > minSymmetryDistance;
        const bool xAligned = c.xSign == c2.xSign && std::abs(c.x - c2.x) < kEps && std::abs(c.y - c2.y) > minAlignDistance;
        const bool yAligned = c.ySign == c2.ySign && std::abs(c.y - c2.y) < kEps && std::abs(c.x - c2.x) > minAlignDistance;
        const bool square = xSymmetric && ySymmetric && std::abs(std::abs(c.x) - std::abs(c.y)) < kEps;
        if (pass > 0) {
            if (xSymmetric && c2.yMirrorBuddy)
                if (!c.xSymmetricBuddy || yDistance(c, *c.xSymmetricBuddy->yMirrorBuddy) < yDistance(c, *c2.yMirrorBuddy))
                    c.xSymmetricBuddy = &c2;
            if (ySymmetric && c2.xMirrorBuddy)
                if (!c.ySymmetricBuddy || xDistance(c, *c.ySymmetricBuddy->xMirrorBuddy) < xDistance(c, *c2.xMirrorBuddy))
                    c.ySymmetricBuddy = &c2;
        }
        if (xAligned && (!c.xAlignedBuddy || yDistance(c, *c.xAlignedBuddy) < yDistance(c, c2))) c.xAlignedBuddy = &c2;
        if (yAligned && (!c.yAlignedBuddy || xDistance(c, *c.yAlignedBuddy) < xDistance(c, c2))) c.yAlignedBuddy = &c2;
        if (xSymmetric && ySymmetric) {
            c.diagonalBuddy = &c2;
            if (square) c.square = true;
        }
        if (xSymmetric && yAligned) c.xMirrorBuddy = &c2;
        if (ySymmetric && xAligned) c.yMirrorBuddy = &c2;
    }
    return pass > 0 && !cornerSolution(c).empty();
}

// Whether `a` and `b` can be seen in one shot: their signs point outwards from each other.
bool pairsInOneShot(const Corner& a, const Corner* b) {
    if (!b) return false;
    if (std::abs(a.x - b->x) > kEps) {
        if (a.xSign == b->xSign || a.xSign * (a.x - b->x) < kEps) return false;
    } else if (a.xSign != b->xSign) {
        return false;
    }
    if (std::abs(a.y - b->y) > kEps) {
        if (a.ySign == b->ySign || a.ySign * (a.y - b->y) < kEps) return false;
    } else if (a.ySign != b->ySign) {
        return false;
    }
    return true;
}

// OpenPnP's Corner.compareTo: by rating (best first), distance from the centre (farthest first), then angle from pin 1.
bool cornerBefore(const std::unique_ptr<Corner>& a, const std::unique_ptr<Corner>& b) {
    if (a->rating != b->rating) return a->rating > b->rating;
    const double d1 = std::hypot(a->x, a->y), d2 = std::hypot(b->x, b->y);
    if (std::abs(d1 - d2) > kEps) return d1 > d2;
    return std::atan2(a->x, -a->y) < std::atan2(b->x, -b->y);
}

// A shot of these corners: their middle (with the origin, as OpenPnP's), sized to them and the tolerance.
JPVisionComposite::Shot shotOf(const std::vector<const Corner*>& corners, double minMask, double maxMask, double tolerance,
                               ShotConfiguration config) {
    JPVisionComposite::Shot s;
    s.corners = corners;
    double x0 = 0, y0 = 0, x1 = 0, y1 = 0, x = 0, y = 0;
    bool optional = true;
    for (const Corner* c : corners) {
        x0 = std::min(x0, c->x);
        x1 = std::max(x1, c->x);
        y0 = std::min(y0, c->y);
        y1 = std::max(y1, c->y);
        x += c->x;
        y += c->y;
        if (!c->optional) optional = false;
    }
    s.x = x / double(corners.size());
    s.y = y / double(corners.size());
    s.width = x1 - x0 + tolerance * 2;
    s.height = y1 - y0 + tolerance * 2;
    s.minMaskRadius = minMask;
    s.maxMaskRadius = maxMask;
    s.optional = optional;
    s.configuration = config;
    return s;
}

} // namespace

const char* JPVisionComposite::solutionName(Solution s) {
    static const char* const kNames[] = { "Square", "Box", "Z", "Arrow", "Figure7", "Angle", "Trapezoid", "Small",
                                          "VisionOffsets", "NoFootprint", "NoCameraRoaming", "RestrictedCameraRoaming",
                                          "Invalid" };
    return kNames[int(s)];
}

const char* JPVisionComposite::configurationName(ShotConfiguration c) {
    static const char* const kNames[] = { "Square", "Box", "MirrorX", "MirrorY", "Corner", "Unknown" };
    return kNames[int(c)];
}

bool JPVisionComposite::Shot::hasLeftEdge() const {
    if (corners.empty()) return true;
    return std::any_of(corners.begin(), corners.end(), [](const Corner* c) { return c->xSign < 0; });
}
bool JPVisionComposite::Shot::hasRightEdge() const {
    if (corners.empty()) return true;
    return std::any_of(corners.begin(), corners.end(), [](const Corner* c) { return c->xSign > 0; });
}
bool JPVisionComposite::Shot::hasTopEdge() const {
    if (corners.empty()) return true;
    return std::any_of(corners.begin(), corners.end(), [](const Corner* c) { return c->ySign > 0; });
}
bool JPVisionComposite::Shot::hasBottomEdge() const {
    if (corners.empty()) return true;
    return std::any_of(corners.begin(), corners.end(), [](const Corner* c) { return c->ySign < 0; });
}

JPVisionComposite::JPVisionComposite(const Input& in) {
    const auto t0 = std::chrono::steady_clock::now();
    compute(in);
    m_computeSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
}

void JPVisionComposite::compute(const Input& in) {
    const JPVisionCompositing& vc = in.compositing;
    m_method = vc.compositingMethod;
    m_extraShots = vc.extraShots;
    m_tolerance = vc.maxPickTolerance.convertToUnits(JPLengthUnit::Millimeters).value();
    if (m_tolerance <= 0) m_tolerance = in.toleranceMm;
    const double maxPartDiameter = in.maxPartDiameterMm + 2 * m_tolerance;
    // The camera's view, limited to the largest part.
    m_cameraViewRadius = std::min(maxPartDiameter, std::min(in.cameraWidthMm, in.cameraHeightMm)) / 2;
    Pad body;
    body.width = in.footprint.bodyWidth;
    body.height = in.footprint.bodyHeight;
    const std::vector<Pad> pads = m_method == Method::Body ? std::vector<Pad> { body } : in.footprint.pads;
    if (pads.empty() || in.visionOffsets || in.roamingRadiusMm == 0) {
        // No footprint, or vision offsets, or no roaming radius: the classic single shot.
        Shot whole;
        whole.width = whole.height = maxPartDiameter;
        whole.minMaskRadius = whole.maxMaskRadius = maxPartDiameter / 2;
        m_shots.push_back(whole);
        if (in.visionOffsets) {
            m_solution = Solution::VisionOffsets;
            m_diagnostics += "BottomVisionSettings " + in.settingsName + " has Vision Offsets, compositing unsupported. ";
        } else if (in.roamingRadiusMm == 0) {
            m_solution = Solution::NoCameraRoaming;
            m_diagnostics += "ReferenceCamera " + in.cameraName + " has no roaming radius set, compositing forbidden. ";
        } else {
            m_solution = Solution::NoFootprint;
            m_diagnostics += "Package " + in.packageId + " has no footprint defined, compositing not possible. ";
        }
        return;
    }
    // The body in the octogonal hull (it must not collide within the roaming radius).
    addPadToOctogonalHull(body);
    // Rectified and fused pads (pads are assumed to be in lines; if not, it is slower but still right).
    std::vector<Pad> rect;
    for (const Pad& pad : pads) {
        if (std::abs(std::fmod(pad.rotation, 90.0)) > kEps && JPVisionCompositing::isEnforced(m_method)) {
            m_solution = Solution::Invalid;
            m_diagnostics += "Package " + in.packageId + " pad " + pad.name + " not at 90° step angle. ";
            Shot whole;
            whole.width = whole.height = maxPartDiameter;
            whole.minMaskRadius = whole.maxMaskRadius = maxPartDiameter / 2;
            m_shots.push_back(whole);
            return;
        }
        rect.push_back(boundingBox(pad));
    }
    // Twice, for columns and rows fused in BGAs.
    m_rectifiedPads = fusedPads(fusedPads(rect));
    for (const Pad& pad : m_rectifiedPads) addPadToOctogonalHull(pad);

    // The pads' edges.
    for (const Pad& pad : m_rectifiedPads) {
        if (vc.allowInside || pad.x - pad.width / 2 < 0) addCoordinate(m_leftEdges, pad.x - pad.width / 2);
        if (vc.allowInside || pad.y - pad.height / 2 < 0) addCoordinate(m_bottomEdges, pad.y - pad.height / 2);
        if (vc.allowInside || pad.x + pad.width / 2 > 0) addCoordinate(m_rightEdges, pad.x + pad.width / 2);
        if (vc.allowInside || pad.y + pad.height / 2 > 0) addCoordinate(m_topEdges, pad.y + pad.height / 2);
    }
    if (m_leftEdges.empty() || m_rightEdges.empty() || m_topEdges.empty() || m_bottomEdges.empty()) {
        m_solution = Solution::Invalid;
        m_diagnostics += "No solution found. The footprint has no outer edges on every side. ";
        Shot whole;
        whole.width = whole.height = maxPartDiameter;
        whole.minMaskRadius = whole.maxMaskRadius = maxPartDiameter / 2;
        m_shots.push_back(whole);
        return;
    }
    const double overallWidth = m_rightEdges.back() - m_leftEdges.front();
    const double overallHeight = m_topEdges.back() - m_bottomEdges.front();
    const double overallDiagonal = std::hypot(overallWidth, overallHeight);
    const bool isSymmetric = std::abs(m_rightEdges.back() + m_leftEdges.front()) < kEps
                          && std::abs(m_topEdges.back() + m_bottomEdges.front()) < kEps;

    // The X and Y edges combined: the eligible corners, those out in the "air" too.
    findEligibleCorners(m_leftEdges, m_bottomEdges, -1, -1, in.roamingRadiusMm);
    findEligibleCorners(m_leftEdges, m_topEdges, -1, +1, in.roamingRadiusMm);
    findEligibleCorners(m_rightEdges, m_bottomEdges, +1, -1, in.roamingRadiusMm);
    findEligibleCorners(m_rightEdges, m_topEdges, +1, +1, in.roamingRadiusMm);

    // The corners' buddies, and the best solution.
    std::vector<Corner*> solution;
    const double minLeverage = std::min(overallWidth, overallHeight) * vc.minLeverageFactor;
    Solution best = Solution::Small;
    if (computeBuddies(minLeverage) > 0) {
        std::stable_sort(m_corners.begin(), m_corners.end(), cornerBefore);
        Corner& main = *m_corners.front();
        solution = cornerSolution(main);
        best = main.solution;
    }
    const double minRadius = overallDiagonal / 2 + m_tolerance;
    std::vector<const Corner*> solutionCorners(solution.begin(), solution.end());
    if (m_method == Method::None || (isSymmetric && minRadius < m_cameraViewRadius && !JPVisionCompositing::isEnforced(m_method))) {
        // The whole footprint fits in the camera.
        Shot whole;
        whole.corners = solutionCorners;
        whole.width = overallWidth + 2 * m_tolerance;
        whole.height = overallHeight + 2 * m_tolerance;
        whole.minMaskRadius = minRadius;
        whole.maxMaskRadius = m_cameraViewRadius;
        m_shots.push_back(whole);
        if (minRadius < m_cameraViewRadius) {
            best = Solution::Small;
        } else {
            best = Solution::RestrictedCameraRoaming;
            m_diagnostics += std::string("Compositing method ") + JPVisionCompositing::methodName(m_method) + " blocks compositing. ";
        }
    } else if (!solution.empty()) {
        composeShots(solution);
    }
    if (m_shots.empty()) {
        // No solution: a single shot, but invalid.
        Shot whole;
        for (const auto& c : m_corners) whole.corners.push_back(c.get());
        whole.width = overallWidth + 2 * m_tolerance;
        whole.height = overallHeight + 2 * m_tolerance;
        whole.minMaskRadius = minRadius;
        whole.maxMaskRadius = m_cameraViewRadius;
        m_shots.push_back(whole);
        if (m_outOfRoamingCandidates > 0) {
            best = Solution::RestrictedCameraRoaming;
            m_diagnostics += "ReferenceCamera " + in.cameraName + " has insufficient roaming radius, compositing blocked. ";
        } else {
            best = Solution::Invalid;
            m_diagnostics += "No solution found. Cannot isolate corners with compositable X and Y symmetries. Check footprint. ";
        }
    }
    m_solution = best;
}

std::vector<Pad> JPVisionComposite::fusedPads(const std::vector<Pad>& pads) const {
    std::vector<Pad> fused;
    std::optional<Pad> running;
    for (const Pad& pad : pads) {
        if (!running) {
            running = pad;
            continue;
        }
        std::optional<Pad> f;
        const Pad& r = *running;
        if (std::abs(r.y - pad.y) < kEps && std::abs(r.height - pad.height) < kEps) {
            if (std::abs(r.x - pad.x) < r.width / 2 + pad.width / 2 + m_tolerance) {
                // Fused left to right.
                const double x0 = std::min(r.x - r.width / 2, pad.x - pad.width / 2);
                const double x1 = std::max(r.x + r.width / 2, pad.x + pad.width / 2);
                Pad p;
                p.x = (x0 + x1) / 2;
                p.width = x1 - x0;
                p.y = r.y;
                p.height = r.height;
                f = p;
            }
        } else if (std::abs(r.x - pad.x) < kEps && std::abs(r.width - pad.width) < kEps) {
            if (std::abs(r.y - pad.y) < r.height / 2 + pad.height / 2 + m_tolerance) {
                // Fused top to bottom.
                const double y0 = std::min(r.y - r.height / 2, pad.y - pad.height / 2);
                const double y1 = std::max(r.y + r.height / 2, pad.y + pad.height / 2);
                Pad p;
                p.x = r.x;
                p.width = r.width;
                p.y = (y0 + y1) / 2;
                p.height = y1 - y0;
                f = p;
            }
        }
        if (!f) {
            fused.push_back(r);
            running = pad;
        } else {
            running = f;
        }
    }
    if (running) fused.push_back(*running);
    return fused;
}

void JPVisionComposite::addPadToOctogonalHull(const Pad& pad) {
    for (const auto& pt : padCorners(pad)) m_maxPadRadius = std::max(m_maxPadRadius, std::hypot(pt.x, pt.y));
    for (int i = 0; i < 8; ++i) {
        const double h = pad.x * kXHull[i] + pad.width * std::abs(kXHull[i]) * 0.5 + pad.y * kYHull[i]
                       + pad.height * std::abs(kYHull[i]) * 0.5;
        m_octogonalHull[i] = std::max(m_octogonalHull[i], h);
    }
}

double JPVisionComposite::requiredRoamingRadius(double x, double y) const {
    double r = 0;
    for (int i = 0; i < 8; ++i) r = std::max(r, m_octogonalHull[i] - (x * kXHull[i] + y * kYHull[i]));
    return r;
}

int JPVisionComposite::computeBuddies(double minLeverage) {
    int count = 0;
    for (int pass = 0; pass < 2; ++pass)
        for (const auto& c : m_corners)
            if (computeCornerBuddies(*c, m_corners, minLeverage, minLeverage / 2, pass)) ++count;
    return count;
}

void JPVisionComposite::findEligibleCorners(const std::vector<double>& xEdges, const std::vector<double>& yEdges, int xSign,
                                            int ySign, double roamingRadius) {
    for (const double yEdge : yEdges)
        for (const double xEdge : xEdges) {
            if (requiredRoamingRadius(xEdge, yEdge) > roamingRadius) {
                ++m_outOfRoamingCandidates;   // the part cannot be moved that far
                continue;
            }
            // The pads near it.
            double nearestOutside = m_cameraViewRadius * 2;
            double nearestEdgeX = INFINITY, nearestEdgeY = INFINITY;
            std::vector<const Pad*> edgeXPads, edgeYPads;
            for (const Pad& pad : m_rectifiedPads) {
                const double x = pad.x + xSign * pad.width / 2;
                const double y = pad.y + ySign * pad.height / 2;
                if (xSign * (x - xEdge) > kEps || ySign * (y - yEdge) > kEps) {
                    // Outside the corner.
                    nearestOutside = std::min(nearestOutside, padDistance(xEdge, yEdge, pad));
                } else {
                    if (std::abs(x - xEdge) < kEps) {
                        nearestEdgeX = std::min(nearestEdgeX, padDistance(xEdge, yEdge, pad));
                        edgeXPads.push_back(&pad);
                    }
                    if (std::abs(y - yEdge) < kEps) {
                        nearestEdgeY = std::min(nearestEdgeY, padDistance(xEdge, yEdge, pad));
                        edgeYPads.push_back(&pad);
                    }
                }
            }
            // Edge pads that define the edge well enough: with the corner off the pads (a Quad
            // package), the edge they give must be at least as long as the way to the first pad
            // (twice its distance, to the next pad), or the MinAreaRect might find a diagonal box.
            const double minOverlap = m_cameraViewRadius * kMinCameraRadiusOverlap;
            const double edgePadDistance = std::max(nearestEdgeX, nearestEdgeY);
            auto overlapAlong = [&](const std::vector<const Pad*>& edgePads, bool alongX) {
                const double wanted = edgePadDistance * 2 + minOverlap;
                double overlap = INFINITY;
                for (const Pad* pad : edgePads) {
                    const double distance = padDistance(xEdge, yEdge, *pad);
                    if (distance + minOverlap > wanted) {
                        overlap = std::min(overlap, distance + minOverlap);   // beyond: the overlap extended
                    } else if (wanted < distance + (alongX ? pad->width : pad->height)) {
                        if (overlap > wanted) {   // across: just what is wanted
                            overlap = wanted;
                            break;
                        }
                    }
                }
                return overlap;
            };
            const double overlap = std::max(overlapAlong(edgeYPads, true), overlapAlong(edgeXPads, false));
            if (overlap + m_tolerance < nearestOutside - m_tolerance && overlap + m_tolerance < m_cameraViewRadius) {
                auto c = std::make_unique<Corner>();
                c->x = xEdge;
                c->y = yEdge;
                c->minMaskRadius = overlap + m_tolerance;
                c->maxMaskRadius = nearestOutside - m_tolerance;
                c->xSign = xSign;
                c->ySign = ySign;
                m_corners.push_back(std::move(c));
            }
        }
}

void JPVisionComposite::composeShots(std::vector<Corner*> solution) {
    m_maxCornerRadius = 0;
    for (const Corner* c : solution) m_maxCornerRadius = std::max(m_maxCornerRadius, std::hypot(c->x, c->y));
    auto contains = [&solution](const Corner* c) { return std::find(solution.begin(), solution.end(), c) != solution.end(); };
    auto remove = [&solution](const Corner* c) { solution.erase(std::find(solution.begin(), solution.end(), c)); };
    while (!solution.empty()) {
        Corner* corner = solution.front();
        solution.erase(solution.begin());
        double bestDistance = -INFINITY, bestOverreach = 0;
        std::optional<ShotConfiguration> bestConfig;
        Corner *bestBuddy = nullptr, *bestBuddy2 = nullptr, *bestBuddy3 = nullptr;
        if (m_method != Method::SingleCorners) {
            for (Corner* buddy : solution) {
                if (!pairsInOneShot(*corner, buddy)) continue;
                std::optional<ShotConfiguration> config;
                Corner *buddy2 = nullptr, *buddy3 = nullptr;
                if (corner->diagonalBuddy == buddy) {
                    if (pairsInOneShot(*corner, corner->xMirrorBuddy) && contains(corner->xMirrorBuddy)) buddy2 = corner->xMirrorBuddy;
                    if (pairsInOneShot(*corner, corner->yMirrorBuddy) && contains(corner->yMirrorBuddy)) buddy3 = corner->yMirrorBuddy;
                    if (corner->square) config = ShotConfiguration::Square;
                    else if (buddy2 || buddy3) config = ShotConfiguration::Box;
                } else if (corner->xMirrorBuddy == buddy || corner->yMirrorBuddy == buddy) {
                    config = ShotConfiguration::MirrorY;   // both, as OpenPnP's
                }
                if (!config) continue;
                const double distance = std::hypot(xDistance(*corner, *buddy), yDistance(*corner, *buddy));
                const double overreach = std::min({ (m_cameraViewRadius - m_tolerance) * 2 - distance,
                                                    corner->maxMaskRadius - distance, buddy->maxMaskRadius - distance });
                double minOverreach = 0;
                if (*config != ShotConfiguration::Square && *config != ShotConfiguration::Box) {
                    // A horizontal or vertical buddy: the least mask radius must be covered too
                    // (a diagonal one has the radius all round).
                    const double minMask = std::max(corner->minMaskRadius, buddy->minMaskRadius);
                    minOverreach = std::hypot(minMask, distance / 2) - distance / 2;
                }
                if (overreach > minOverreach + m_tolerance) {
                    // They reach each other.
                    if (!bestConfig || int(*bestConfig) > int(*config) || (*bestConfig == *config && bestDistance < distance)) {
                        bestDistance = distance;
                        bestOverreach = overreach;
                        bestConfig = config;
                        bestBuddy = buddy;
                        bestBuddy2 = buddy2;
                        bestBuddy3 = buddy3;
                    }
                }
            }
        }
        std::vector<const Corner*> shotCorners { corner };
        if (bestBuddy) {
            remove(bestBuddy);
            shotCorners.push_back(bestBuddy);
            if (bestBuddy2) {
                remove(bestBuddy2);
                shotCorners.push_back(bestBuddy2);
            }
            if (bestBuddy3) {
                remove(bestBuddy3);
                shotCorners.push_back(bestBuddy3);
            }
            m_shots.push_back(shotOf(shotCorners, bestDistance / 2 + m_tolerance,
                                     std::min(m_cameraViewRadius, bestDistance / 2 + bestOverreach), m_tolerance, *bestConfig));
        } else {
            m_shots.push_back(shotOf(shotCorners, corner->minMaskRadius, std::min(m_cameraViewRadius, corner->maxMaskRadius),
                                     m_tolerance, ShotConfiguration::Corner));
        }
    }
}

std::vector<const JPVisionComposite::Shot*> JPVisionComposite::travel(double fromX, double fromY) const {
    std::vector<const Shot*> visited;
    int extra = 0;
    for (const Shot& s : m_shots) {
        if (!s.optional) visited.push_back(&s);
        else if (extra < m_extraShots) {
            visited.push_back(&s);
            ++extra;
        }
    }
    // The shortest way through them from where the nozzle is, left open at the end (the
    // nozzle goes to -shot: the shot's corner over the camera).
    auto at = [](const Shot* s) { return Point { -s->x, -s->y }; };
    auto length = [&](const std::vector<const Shot*>& order) {
        double d = 0;
        Point p { fromX, fromY };
        for (const Shot* s : order) {
            const Point q = at(s);
            d += std::hypot(q.x - p.x, q.y - p.y);
            p = q;
        }
        return d;
    };
    if (visited.size() <= 8) {
        std::vector<const Shot*> order = visited, best = visited;
        std::sort(order.begin(), order.end());
        double bestLength = INFINITY;
        do {
            if (const double l = length(order); l < bestLength - kEps) {
                bestLength = l;
                best = order;
            }
        } while (std::next_permutation(order.begin(), order.end()));
        return best;
    }
    // Many: nearest next.
    std::vector<const Shot*> left = visited, order;
    Point p { fromX, fromY };
    while (!left.empty()) {
        auto next = std::min_element(left.begin(), left.end(), [&](const Shot* a, const Shot* b) {
            return std::hypot(at(a).x - p.x, at(a).y - p.y) < std::hypot(at(b).x - p.x, at(b).y - p.y);
        });
        p = at(*next);
        order.push_back(*next);
        left.erase(next);
    }
    return order;
}

void JPVisionComposite::accumulate(const Shot& shot, const std::array<Point, 4>& points) {
    for (const Corner* c : shot.corners) m_cornerMap[c] = points[size_t((c->xSign < 0 ? 0 : 1) + (c->ySign > 0 ? 0 : 2))];
}

bool JPVisionComposite::interpret(double expectedAngle, Detected& out, std::string& why) const {
    Point centerSum;
    int centerWeights = 0, angleWeights = 0;
    double angleSum = 0, xScaleSum = 0, xScaleWeights = 0, yScaleSum = 0, yScaleWeights = 0;
    auto get = [this](const Corner* c) -> const Point* {
        if (!c) return nullptr;
        const auto it = m_cornerMap.find(c);
        return it == m_cornerMap.end() ? nullptr : &it->second;
    };
    auto nearest = [expectedAngle](double angle) { return angle + std::round((expectedAngle - angle) / 90) * 90; };
    for (const auto& [corner, p1] : m_cornerMap) {
        // Diagonal.
        if (const Point* p2 = get(corner->diagonalBuddy)) {
            centerSum.x += p1.x + p2->x;
            centerSum.y += p1.y + p2->y;
            centerWeights += 2;
            if (corner->square) {
                // A square's diagonal gives the angle too (at 45°), and the scale.
                const double dx = p2->x - p1.x, dy = p2->y - p1.y;
                angleSum += nearest(std::atan2(dy, dx) * 180 / M_PI - 45);
                ++angleWeights;
                const double distance = std::hypot(dx, dy);
                xScaleSum += distance / std::sqrt(2.0);
                xScaleWeights += std::abs(corner->diagonalBuddy->x - corner->x);
                yScaleSum += distance / std::sqrt(2.0);
                yScaleWeights += std::abs(corner->diagonalBuddy->y - corner->y);
            }
        }
        // Mirrors in pairs, in Y and in X.
        for (const bool inY : { true, false }) {
            const Corner* sym = inY ? corner->ySymmetricBuddy : corner->xSymmetricBuddy;
            if (!sym) continue;
            const Point* p2 = get(inY ? corner->xMirrorBuddy : corner->yMirrorBuddy);
            const Point* p3 = get(sym);
            const Point* p4 = get(inY ? sym->xMirrorBuddy : sym->yMirrorBuddy);
            if (!p2 || !p3 || !p4) continue;
            centerSum.x += p1.x + p2->x + p3->x + p4->x;
            centerSum.y += p1.y + p2->y + p3->y + p4->y;
            centerWeights += 4;
            const double distance = std::hypot((p3->x + p4->x) / 2 - (p1.x + p2->x) / 2, (p3->y + p4->y) / 2 - (p1.y + p2->y) / 2);
            if (inY) {
                yScaleSum += distance;
                yScaleWeights += std::abs(sym->y - corner->y);
            } else {
                xScaleSum += distance;
                xScaleWeights += std::abs(sym->x - corner->x);
            }
        }
        // Aligned and mirrored: an angle and a scale.
        for (const Corner* buddy : { corner->xAlignedBuddy, corner->yAlignedBuddy, corner->xMirrorBuddy, corner->yMirrorBuddy }) {
            const Point* p2 = get(buddy);
            if (!p2) continue;
            const double dx = p2->x - p1.x, dy = p2->y - p1.y;
            angleSum += nearest(std::atan2(dy, dx) * 180 / M_PI);
            ++angleWeights;
            const double distance = std::hypot(dx, dy);
            const double bx = std::abs(buddy->x - corner->x), by = std::abs(buddy->y - corner->y);
            if (bx > by) {
                xScaleSum += distance;
                xScaleWeights += bx;
            } else {
                yScaleSum += distance;
                yScaleWeights += by;
            }
        }
    }
    if (m_leftEdges.empty() || centerWeights == 0) {
        why = "Unable to calculate center from composite vision";
        return false;
    }
    if (angleWeights == 0) {
        why = "Unable to calculate angle from composite vision";
        return false;
    }
    out.center = { centerSum.x / centerWeights, centerSum.y / centerWeights };
    out.angle = nearest(angleSum / angleWeights);
    if (xScaleWeights == 0) xScaleSum = xScaleWeights = 1;   // the X scale cannot be adjusted
    if (yScaleWeights == 0) yScaleSum = yScaleWeights = 1;
    out.scale = { xScaleSum / xScaleWeights, yScaleSum / yScaleWeights };
    out.size = { out.scale.x * std::max(-m_leftEdges.front(), m_rightEdges.back()) * 2,
                 out.scale.y * std::max(-m_bottomEdges.front(), m_topEdges.back()) * 2 };
    return true;
}

} // inline namespace jf
