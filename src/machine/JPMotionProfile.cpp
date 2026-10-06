// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// A port of OpenPnP's MotionProfile (Copyright (C) 2020 <mark@makr.zone>, GPL-3.0-or-later): the solver, its
// regions and analytical guesses, the coordination and synchronization of several axes, kept to OpenPnP's
// structure so the two can be read side by side. OpenPnP's comments are kept where they explain the maths.

#include "JPMotionProfile.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>

inline namespace jf {

namespace {

constexpr int kSeg = JPMotionProfile::kSegments;
constexpr double kInf = std::numeric_limits<double>::infinity();
constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

// Java's Math.signum.
double signum(double x) { return x > 0 ? 1.0 : x < 0 ? -1.0 : x; }
double sq(double x) { return x * x; }
double cube(double x) { return x * x * x; }

bool mismatch(double a, double b, double tol) { return std::abs(a - b) > tol; }

// Newton's method in [x0, x1]: f's root (zeroes) or a local minimum; none when it escapes or stalls.
template <typename F, typename G>
std::optional<double> newtonSolve(double x0, double x1, F f, G g, bool zeroes) {
    double x = (x0 + x1) * 0.5;
    int escapeNeg = 0, escapePos = 0;
    for (int iter = 0; iter < JPMotionProfile::kIterations; iter++) {
        const double y = f(x), dydt = g(x);
        if (std::abs(dydt) < JPMotionProfile::kTtol) return std::nullopt;   // the denominator too small
        const double xn = std::max(x0, std::min(x1, x - y / dydt));
        if (xn <= x0) {
            if (++escapeNeg > 1) return std::nullopt;   // outside several times: escaped
            escapePos = 0;
        } else if (xn >= x1) {
            if (++escapePos > 1) return std::nullopt;
            escapeNeg = 0;
        } else {
            escapeNeg = 0;
            escapePos = 0;
            if (zeroes ? std::abs(y) <= JPMotionProfile::kTtol : std::abs(xn - x) <= JPMotionProfile::kTtol) {
                x = xn;
                break;
            }
        }
        x = xn;
    }
    return x;
}

} // namespace

JPMotionProfile::JPMotionProfile(double s0, double s1, double v0, double v1, double a0, double a1, double sMin_, double sMax_,
                                 double vMax_, double aMaxEntry_, double aMaxExit_, double jMax_, double tMin_, double tMax_,
                                 int options)
    : sMin(sMin_), sMax(sMax_), vMax(vMax_), aMaxEntry(aMaxEntry_), aMaxExit(aMaxExit_), jMax(jMax_), tMin(tMin_), tMax(tMax_),
      m_options(options) {
    s[0] = s0;
    s[kSeg] = s1;
    v[0] = v0;
    v[kSeg] = v1;
    a[0] = a0;
    a[kSeg] = a1;
}

JPMotionProfile JPMotionProfile::like(const JPMotionProfile& p) {
    return JPMotionProfile(p.s[0], p.s[kSeg], p.v[0], p.v[kSeg], p.a[0], p.a[kSeg], p.sMin, p.sMax, p.vMax, p.aMaxEntry,
                           p.aMaxExit, p.jMax, p.tMin, p.tMax, p.m_options);
}

double JPMotionProfile::segmentBeginTime(int segment) const {
    double sum = 0;
    for (int i = 1; i <= segment; i++) sum += t[i];
    return sum;
}

bool JPMotionProfile::isEmpty() const {
    return s[0] == s[kSeg] && v[0] == 0 && v[kSeg] == 0 && a[0] == 0 && a[kSeg] == 0;
}

bool JPMotionProfile::isConstantAcceleration() const { return jMax == 0 || std::isinf(jMax); }

double JPMotionProfile::profileVelocity(JPMotionControlType type) const {
    if (type == JPMotionControlType::EuclideanAxisLimits) return vMax;
    return std::max(std::abs(vBound0), std::abs(vBound1));
}

double JPMotionProfile::profileAcceleration(JPMotionControlType type) const {
    if (type == JPMotionControlType::EuclideanAxisLimits) return std::max(aMaxEntry, aMaxExit);
    return std::max(std::abs(aBound0), std::abs(aBound1));
}

double JPMotionProfile::profileJerk(JPMotionControlType type) const {
    if (type == JPMotionControlType::EuclideanAxisLimits) return jMax;
    return std::max(std::abs(j[0]), std::abs(j[6]));
}

double JPMotionProfile::momentaryLocation(double ts) const {
    if (ts <= t[0]) return s[0];
    ts -= t[0];
    if (ts >= m_time) return s[kSeg];
    for (int i = 1; i <= kSeg; i++) {
        // s0 + V0*t + 1/2*a0*t^2 + 1/6*j*t^3
        if (ts < t[i]) return s[i - 1] + v[i - 1] * ts + 1. / 2 * a[i - 1] * sq(ts) + 1. / 6 * j[i - 1] * cube(ts);
        ts -= t[i];
    }
    return s[kSeg];
}

double JPMotionProfile::momentaryVelocity(double ts) const {
    if (ts <= t[0]) return v[0];
    ts -= t[0];
    if (ts >= m_time) return v[kSeg];
    for (int i = 1; i <= kSeg; i++) {
        if (ts < t[i]) return v[i - 1] + a[i - 1] * ts + 1. / 2 * j[i - 1] * sq(ts);
        ts -= t[i];
    }
    return v[kSeg];
}

double JPMotionProfile::momentaryAcceleration(double ts) const {
    const double end = isConstantAcceleration() ? 0 : a[kSeg];
    if (ts <= t[0]) return a[0];
    ts -= t[0];
    if (ts >= m_time) return end;
    for (int i = 1; i <= kSeg; i++) {
        if (ts < t[i]) return a[i - 1] + j[i - 1] * ts;
        ts -= t[i];
    }
    return end;
}

double JPMotionProfile::momentaryJerk(double ts) const {
    if (ts <= t[0]) return j[0];
    ts -= t[0];
    if (ts >= m_time) return 0;
    for (int i = 1; i <= kSeg; i++) {
        if (ts < t[i]) return j[i - 1];
        ts -= t[i];
    }
    return 0;
}

std::optional<JPMotionProfile::Error> JPMotionProfile::checkValidity() const {
    // Phase 1: hard constraints, continuity.
    double tSum = 0;
    for (int i = 0; i <= kSeg; i++) {
        if (!(std::isfinite(t[i]) && std::isfinite(s[i]) && std::isfinite(v[i]) && std::isfinite(a[i]) && std::isfinite(j[i])))
            return Error::SolutionNotFinite;
        if (t[i] < (i == 4 ? -kTtol : -kEps)) return Error::NegativeSegmentTime;
        tSum += t[i];
        if (i > 0) {
            if (mismatch(s[i], s[i - 1] + v[i - 1] * t[i] + 1. / 2 * a[i - 1] * sq(t[i]) + 1. / 6 * j[i - 1] * cube(t[i]), kEps))
                return Error::LocationDiscontinuity;
            if (mismatch(v[i], v[i - 1] + a[i - 1] * t[i] + 1. / 2 * j[i - 1] * sq(t[i]), kEps)) return Error::VelocityDiscontinuity;
            if (!isConstantAcceleration() && mismatch(a[i], a[i - 1] + j[i - 1] * t[i], kEps)) return Error::AccelerationDiscontinuity;
        }
    }
    tSum += t[kSeg + 1];
    // The wait before and after.
    if (t[0] > kEps && v[0] != 0) return Error::VelocityDiscontinuity;
    if (t[kSeg + 1] > kEps && v[kSeg] != 0) return Error::VelocityDiscontinuity;
    if (!isConstantAcceleration()) {
        if (t[0] > kEps && a[0] != 0) return Error::AccelerationDiscontinuity;
        if (t[kSeg + 1] > kEps && a[kSeg] != 0) return Error::AccelerationDiscontinuity;
    }
    // Time sum constraints.
    if (mismatch(tSum, m_time, kEps)) return Error::TimeSumMismatch;
    if (tSum < tMin - kEps) return Error::MinTimeViolated;
    if (tSum > tMax + kEps) return Error::MaxTimeViolated;
    // The bounds against sMin/sMax.
    if (sBound0 < sMin - kEps) return Error::MinLocationViolated;
    if (sBound1 > sMax + kEps) return Error::MaxLocationViolated;
    // Phase 2: lesser constraints.
    for (int i = 0; i <= kSeg; i++) {
        if (i < kSeg && s[i] < sMin - kEps) return Error::MinLocationViolated;
        if (i < kSeg && s[i] > sMax + kEps) return Error::MaxLocationViolated;
        if (i < kSeg && std::abs(v[i]) > vMax + kVtol) return Error::MaxVelocityViolated;
        if (i <= kSeg / 2 && std::abs(a[i]) > aMaxEntry + kAtol) return Error::MaxAccelerationViolated;
        if (i > kSeg / 2 && i < kSeg && std::abs(a[i]) > aMaxExit + kAtol) return Error::MaxAccelerationViolated;
        if (std::abs(j[i]) > jMax + kJtol) return Error::MaxJerkViolated;
    }
    return std::nullopt;
}

void JPMotionProfile::solve() {
    // Tolerances scaled down for tiny moves.
    const double magnitude = std::max(kEps, std::min(1.0, 0.01 * (std::abs(s[0] - s[kSeg]) + std::abs(v[0]) + std::abs(v[kSeg])
                                                                   + std::abs(a[0]) + std::abs(a[kSeg]))));
    solve(kIterations, kVtol * std::sqrt(magnitude), kTtol * std::sqrt(magnitude));
}

void JPMotionProfile::solve(int iterations, double vtol, double ttol) {
    solveForVelocity(iterations, vtol, ttol);
    setOption(Solved);
}

bool JPMotionProfile::solveForVelocity(int iterations, double vtol, double ttol) {
    // A null move: as all the machine's axes are always handled, fast with those.
    if (solveIfNullMove()) return true;

    // The effective entry/exit velocity after jerk to acceleration 0.
    const double vEffEntry = effectiveEntryVelocity(jMax);
    const double vEffExit = effectiveExitVelocity(jMax);
    // The direction of travel.
    const double sgn = profileSignum(vEffEntry, vEffExit);

    // The profile with directional vMax first: the first indications, and may directly solve a long move.
    computeProfile(sgn * vMax, vEffEntry, vEffExit, tMin);
    if (tMin == 0 && t[4] >= 0 && v[0] == v[kSeg] && a[0] == 0 && a[kSeg] == 0) return true;   // Vmax symmetrical move

    // Numerically: as the solution can have many roots and local minima, split into regions of known qualities.
    double bestTime = kInf;
    double bestVelocity = kNaN;
    const double nearZero = 0;
    std::array<double, 7> borders { -vMax, -nearZero, 0, nearZero, vMax, std::max(-vMax, std::min(vMax, vEffEntry)),
                                    std::max(-vMax, std::min(vMax, vEffExit)) };
    std::sort(borders.begin(), borders.end());
    std::vector<std::array<int, 2>> regions;
    std::array<double, 7> borderSResult, borderTResult {};
    borderSResult.fill(kNaN);
    int i0 = 0, iVMax = -1;
    for (int i = 1; i < int(borders.size()); i++) {
        if (borders[i0] < borders[i]) {
            regions.push_back({ i0, i });
            i0 = i;
            if (borders[i] == v[4]) iVMax = i;
        }
    }
    // The vMax results from the first calculation.
    if (iVMax >= 0) {
        borderSResult[iVMax] = s[4] - s[3];
        borderTResult[iVMax] = m_time;
    }
    const double vttol = std::sqrt(ttol);
    const double stol = ttol;

    // Still from the first calculation, an analytical solution.
    double vInitialGuess = kNaN;
    if (tMin == 0 && s[0] != s[kSeg]) {
        if (isConstantAcceleration()) {
            // Sage: v == +-sqrt(a*s + 1/2*v0^2 + v0*v7 - 1/2*v7^2)
            if (aMaxEntry == aMaxExit) {
                const double sd = sgn * (s[kSeg] - s[0]);
                vInitialGuess = sgn * std::sqrt(aMaxEntry * sd + 1. / 2 * sq(v[0]) + v[0] * v[7] - 1. / 2 * sq(v[7]));
            }
        } else if (!hasOption(SimplifiedSCurve)) {
            // Long enough to reach aMax: vPeak analytically (Sage, with a constant acceleration segment).
            const bool halfProfile = !(hasOption(UnconstrainedEntry) || hasOption(UnconstrainedExit));
            if (t[2] > (-t[4] * 0.25)) {
                // The acceleration segment long enough.
                const double s3 = ((halfProfile ? (s[4] + s[3]) * 0.5 : s[4]) - s[1]);
                vInitialGuess = -1. / 6 * (3 * sq(a[1]) - 2 * std::sqrt(3 * std::pow(a[1], 4) + 18 * a[1] * sq(j[0]) * s3 + 9 * sq(j[0]) * sq(v[1]))) / j[0];
            } else if (t[5] > (-t[4] * 0.25)) {
                // The deceleration segment long enough.
                const double s4 = (s[6] - (halfProfile ? (s[4] + s[3]) * 0.5 : s[3]));
                vInitialGuess = (-1. / 6 * (3 * sq(a[6]) - 2 * std::sqrt(3 * std::pow(a[6], 4) - 18 * a[6] * sq(j[6]) * s4 + 9 * sq(j[6]) * sq(v[6]))) / j[6]);
            }
        }
        if (std::isfinite(vInitialGuess) && std::abs(vInitialGuess) > 0 && std::abs(vInitialGuess) <= vMax) {
            computeProfile(vInitialGuess, vEffEntry, vEffExit, tMin);
            if (t[4] >= -ttol && t[4] < vttol) return true;
        }
    }

    // The border cases.
    for (int i = 0; i < int(borders.size()); i++) {
        if (i == 0 || borders[i - 1] < borders[i]) {
            const double vPeak = borders[i];
            if (std::isnan(borderSResult[i])) {
                computeProfile(vPeak, vEffEntry, vEffExit, tMin);
                borderSResult[i] = s[4] - s[3];
                borderTResult[i] = m_time;
            }
            const double sign = signum(vPeak);
            const double sResult = (sign == 0 ? -std::abs(borderSResult[i]) : sign * borderSResult[i]);   // s != 0 invalid at V == 0
            double tResult = borderTResult[i];
            if (std::isinf(tResult)) {
                tResult = sResult > 0 ? kInf : -kInf;
            } else if (sResult >= -stol && (tMin == 0 || tResult >= tMin - ttol) && tResult < bestTime) {
                // A valid border case is a solution.
                bestVelocity = vPeak;
                bestTime = tResult;
            }
        }
    }

    // Each region, in the order most likely to eclipse the later ones.
    const int regionCount = int(regions.size());
    int regionStart, regionEnd, regionStep;
    if (sgn >= 0) {
        regionStart = regionCount - 1;
        regionEnd = -1;
        regionStep = -1;
    } else {
        regionStart = 0;
        regionEnd = regionCount;
        regionStep = 1;
    }
    for (int regionIndex = regionStart; regionIndex != regionEnd; regionIndex += regionStep) {
        const int border0 = regions[regionIndex][0], border1 = regions[regionIndex][1];
        const double vPeak0 = borders[border0], vPeak1 = borders[border1];
        const double sign = signum(vPeak0 + vPeak1);
        const double sResult0 = sign * borderSResult[border0];
        const double sResult1 = sign * borderSResult[border1];
        const bool sValid0 = (sResult0 >= -stol), sValid1 = (sResult1 >= -stol);
        if (!(sValid0 || sValid1)) continue;   // none valid
        double tResult0 = borderTResult[border0];
        if (std::isinf(tResult0)) tResult0 = sResult0 > 0 ? kInf : -kInf;
        double tResult1 = borderTResult[border1];
        if (std::isinf(tResult1)) tResult1 = sResult1 > 0 ? kInf : -kInf;
        const bool tValid0 = (tMin == 0 || tResult0 >= tMin - ttol), tValid1 = (tMin == 0 || tResult1 >= tMin - ttol);
        if (!(tValid0 || tValid1)) continue;
        if (std::min(tResult0, tResult1) >= bestTime) continue;   // eclipsed

        auto take = [&] {
            if (m_time < bestTime) {
                bestVelocity = v[4];
                bestTime = m_time;
            }
        };
        if (sValid0 && sValid1 && std::min(vEffEntry, vEffExit) <= vPeak0 && std::max(vEffEntry, vEffExit) >= vPeak1) {
            // Between the entry and exit velocities: two successive ramps both accelerating or decelerating, no
            // "rounded trapezoid". The segment 4 distance then has pointy peaks at the entry/exit velocities and a
            // basin between, so there can be two roots: split the region where segment 4 is negative. OpenPnP's
            // working hypothesis: the minimum leans to the higher absolute velocity, the curve is always upward
            // curved, so the secant method falls short when there is a solution, and a rising curve means none.
            const double vSearch0 = vPeak0, vSearch1 = vPeak1;
            double vSecant = sign > 0 ? vPeak0 : vPeak1;
            double sSecant = sign > 0 ? sResult0 : sResult1;
            double sResult, tResult;
            double vSearch = (vPeak0 + vPeak1) * 0.5;
            while (true) {
                computeProfile(vSearch, vEffEntry, vEffExit, tMin);
                sResult = sign * (s[4] - s[3]);
                tResult = m_time;
                if (sResult < 0) break;          // found it
                if (sResult > sSecant) break;    // rising: overshot, no invalid section
                const double gradient = (sResult - sSecant) / (vSecant - vSearch);
                if (std::abs(gradient) < vttol) break;   // stuck in a local minimum: a tangent, not supported
                const double delta = -sResult / gradient;
                vSecant = vSearch;
                sSecant = sResult;
                vSearch = std::max(vSearch0, std::min(vSearch1, vSearch + delta));
            }
            if (sResult < 0) {
                // An invalid section: two roots.
                if (solveRegion(vPeak0, vSearch, sResult0, sResult, tResult0, tResult, vEffEntry, vEffExit, tMin, bestTime,
                                iterations, stol, vtol, ttol))
                    take();
                if (solveRegion(vSearch, vPeak1, sResult, sResult1, tResult, tResult1, vEffEntry, vEffExit, tMin, bestTime,
                                iterations, stol, vtol, ttol))
                    take();
            } else if (solveRegion(vPeak0, vPeak1, sResult0, sResult1, tResult0, tResult1, vEffEntry, vEffExit, tMin, bestTime,
                                   iterations, stol, vtol, ttol)) {
                take();
            }
        } else if (solveRegion(vPeak0, vPeak1, sResult0, sResult1, tResult0, tResult1, vEffEntry, vEffExit, tMin, bestTime,
                               iterations, stol, vtol, ttol)) {
            take();
        }
    }
    if (bestVelocity != v[4]) computeProfile(bestVelocity, vEffEntry, vEffExit, tMin);   // the best again
    if (tMin > 0 && tMin != m_time && std::abs(m_time / tMin - 1) < 0.001) retimeProfile();   // stretched into the exact time
    return true;
}

bool JPMotionProfile::solveIfNullMove() {
    if (s[0] == s[kSeg] && v[0] == v[kSeg] && (isConstantAcceleration() || a[0] == a[kSeg]) && (tMin == 0 || (v[0] == 0 && a[0] == 0))) {
        t[0] = 0;
        j[0] = 0;
        for (int i = 1; i < kSeg; i++) {
            s[i] = s[0];
            v[i] = v[0];
            a[i] = a[0];
            j[i] = j[0];
            t[i] = 0;
        }
        t[kSeg] = 0;
        t[kSeg + 1] = 0;
        j[kSeg] = 0;
        t[4] = tMin;
        m_time = tMin;
        sBound0 = sBound1 = s[0];
        tSBound0 = 0;
        tSBound1 = tMin;
        vBound0 = vBound1 = v[0];
        tVBound0 = 0;
        tVBound1 = tMin;
        aBound0 = aBound1 = a[0];
        tABound0 = 0;
        tABound1 = tMin;
        return true;
    }
    return false;
}

double JPMotionProfile::profileSignum(double vEffEntry, double vEffExit) const {
    double sgn = signum(s[kSeg] - s[0]);
    // Zero displacement: the entry/exit velocity balance decides (completely symmetric: 0, computeProfile copes).
    if (sgn == 0 && std::abs(-vEffEntry - vEffExit) > kEps) sgn = signum(-vEffEntry - vEffExit);
    return sgn;
}

bool JPMotionProfile::solveRegion(double vPeak0, double vPeak1, double sResult0, double sResult1, double tResult0, double tResult1,
                                  double vEffEntry, double vEffExit, double tMin_, double bestTime, int iterations, double stol,
                                  double vtol, double ttol) {
    if (std::min(tResult0, tResult1) >= bestTime) return false;   // eclipsed
    if (bestTime == tMin_) return false;
    double vSecant, sign;
    if (std::abs(vPeak0) < std::abs(vPeak1)) {
        vSecant = vPeak1;
        sign = 1;
    } else {
        vSecant = vPeak0;
        sign = -1;
    }
    double vPeak = (vPeak1 + vPeak0) * 0.5;   // the mid-point first
    int converging = 0;
    double vValid = kNaN;
    bool hasValid = false;
    const bool sAscending = sResult0 < sResult1;
    const bool tAscending = tResult0 < tResult1;
    const double maxMagnitude = std::min((vPeak1 - vPeak0) * 0.00001, 1.0);
    for (int iter = 0; iter <= iterations; iter++) {
        computeProfile(vPeak, vEffEntry, vEffExit, tMin_);
        const double sResult = sign * (s[4] - s[3]);
        const double tResult = m_time;
        const double magnitude = std::max(kEps, std::min(maxMagnitude, 0.0001 * (std::abs(s[3] - s[0]) + std::abs(s[kSeg] - s[4]))));
        if (std::abs(vPeak - vSecant) < magnitude * vtol) converging++;
        else converging = 0;
        if (sResult >= -stol && tResult >= tMin_ - ttol) {
            // Valid, but maybe not optimal (yet).
            if (tResult < tMin_ + ttol || converging >= 2) return true;
            vValid = vPeak;
            hasValid = true;
        } else if (converging >= 4) {
            // Converged, not valid: the previous valid one, if any.
            if (hasValid) {
                computeProfile(vValid, vEffEntry, vEffExit, tMin_);
                return true;
            }
            return false;
        }
        if (sResult >= 0) {
            // Valid continuity: optimized by time.
            if (tAscending ^ (tResult > tMin_)) vPeak0 = vPeak;
            else vPeak1 = vPeak;
        } else {
            if (sAscending) vPeak0 = vPeak;
            else vPeak1 = vPeak;
        }
        vSecant = vPeak;
        vPeak = (vPeak0 + vPeak1) * 0.5;
    }
    return false;
}

bool JPMotionProfile::retimeProfile() {
    if (solveIfNullMove()) return true;
    if (tMin != m_time && tMin > 0.0 && m_time > 0) {
        // The derivatives scaled to the power of their order.
        const double vFactor = m_time / tMin, aFactor = vFactor * vFactor, jFactor = aFactor * vFactor;
        for (int i = 0; i <= kSeg; i++) {
            t[i] /= vFactor;
            v[i] *= vFactor;
            a[i] *= aFactor;
            j[i] *= jFactor;
        }
        t[kSeg + 1] /= vFactor;
        m_time = tMin;
        computeBounds();
        return true;
    }
    return false;
}

bool JPMotionProfile::assertSolved() {
    if (!hasOption(Solved)) {
        solve();
        return true;
    }
    return false;
}

void JPMotionProfile::coordinateProfiles(std::vector<JPMotionProfile>& profiles) {
    // The lead: the axis moving the most.
    JPMotionProfile* lead = nullptr;
    double bestDist = -kInf;
    for (JPMotionProfile& p : profiles) {
        const double dist = std::abs(p.s[kSeg] - p.s[0]);
        if (dist > bestDist) {
            lead = &p;
            bestDist = dist;
        }
    }
    if (lead) {
        lead->assertSolved();
        coordinateProfilesToLead(profiles, *lead);
    }
}

void JPMotionProfile::coordinateProfilesToLead(std::vector<JPMotionProfile>& profiles, const JPMotionProfile& lead) {
    for (JPMotionProfile& p : profiles)
        if (&p != &lead) p.coordinateProfileToLead(lead);
}

void JPMotionProfile::coordinateProfileToLead(const JPMotionProfile& lead) {
    const double leadDist = lead.s[kSeg] - lead.s[0];
    const double factor = leadDist == 0 ? 0 : (s[kSeg] - s[0]) / leadDist;
    if (factor == 0) {
        for (int i = 0; i <= kSeg; i++) {
            t[i] = 0;
            s[i] = s[0];
            v[i] = 0;
            a[i] = 0;
            j[i] = 0;
        }
        t[4] = lead.m_time;
    } else {
        for (int i = 0; i <= kSeg; i++) {
            t[i] = lead.t[i];
            s[i] = (lead.s[i] - lead.s[0]) * factor + s[0];
            v[i] = lead.v[i] * factor;
            a[i] = lead.a[i] * factor;
            j[i] = lead.j[i] * factor;
        }
    }
    m_time = lead.m_time;
    tMin = lead.tMin;
    computeBounds();
    // As if solved.
    m_eval = 0;
    setOption(Solved);
}

void JPMotionProfile::synchronizeProfiles(std::vector<JPMotionProfile>& profiles) {
    // The longest time.
    double maxTime = 0;
    const JPMotionProfile* lead = nullptr;
    for (JPMotionProfile& p : profiles) {
        p.assertSolved();
        if (p.m_time > maxTime) {
            maxTime = p.m_time;
            lead = &p;
        }
    }
    // The others re-timed.
    bool restart;
    do {
        restart = false;
        for (JPMotionProfile& profile : profiles) {
            profile.assertSolved();
            profile.tMin = maxTime;
            if (profile.m_time == maxTime) continue;
            if (profile.hasOption(SynchronizeStraighten) && (profile.v[0] == 0 || profile.v[kSeg] == 0)
                && (profile.isConstantAcceleration() || profile.a[0] == 0 || profile.a[kSeg] == 0)) {
                // Some zero entry/exit velocity/acceleration: straightened, if it can be.
                if (lead) {
                    std::optional<JPMotionProfile> coordinated;
                    // Full coordination.
                    const double leadDist = std::abs(lead->s[kSeg] - lead->s[0]);
                    if (leadDist > kEps) {
                        const double factor = std::abs((profile.s[kSeg] - profile.s[0]) / leadDist);
                        if (factor > 0) {
                            double aFactor = factor;
                            if (lead->isConstantAcceleration() && !profile.isConstantAcceleration()) aFactor = std::sqrt(aFactor);
                            else if (profile.isConstantAcceleration() && !lead->isConstantAcceleration()) aFactor = 0.71;
                            coordinated = like(profile);
                            if (profile.v[0] == 0 && profile.v[kSeg] == 0) coordinated->setVelocityMax(lead->vMax * factor);
                            if (profile.isConstantAcceleration() || profile.a[0] == 0)
                                coordinated->setEntryAccelerationMax(lead->aMaxEntry * aFactor);
                            if (profile.isConstantAcceleration() || profile.a[kSeg] == 0)
                                coordinated->setExitAccelerationMax(lead->aMaxExit * aFactor);
                            if (!profile.isConstantAcceleration() && !lead->isConstantAcceleration())
                                coordinated->setJerkMax(lead->jMax * factor);
                            coordinated->solve();
                            if (coordinated->m_time <= maxTime && !coordinated->checkValidity()) {
                                profile.copyProfileSolution(*coordinated);
                                continue;
                            }
                            coordinated.reset();
                        }
                    }
                    // Partial coordination.
                    if (profile.v[0] == 0 && (profile.isConstantAcceleration() || profile.a[0] == 0) && lead->v[0] == 0
                        && (lead->isConstantAcceleration() || lead->a[0] == 0)) {
                        double timeFactor = profile.segmentBeginTime(3) / lead->segmentBeginTime(3);
                        timeFactor *= profile.m_time / maxTime;
                        if (timeFactor > 0.0 && timeFactor < 1.0) {
                            coordinated = like(profile);
                            coordinated->setEntryAccelerationMax(lead->aMaxEntry * sq(timeFactor));
                            if (!profile.isConstantAcceleration()) coordinated->setJerkMax(lead->jMax * cube(timeFactor));
                        }
                    }
                    if (profile.v[kSeg] == 0 && (profile.isConstantAcceleration() || profile.a[kSeg] == 0) && lead->v[kSeg] == 0
                        && (lead->isConstantAcceleration() || lead->a[kSeg] == 0)) {
                        double timeFactor = (profile.m_time - profile.segmentBeginTime(4)) / (lead->m_time - lead->segmentBeginTime(4));
                        timeFactor *= profile.m_time / maxTime;
                        if (timeFactor > 0.0 && timeFactor < 1.0) {
                            if (!coordinated) coordinated = like(profile);
                            coordinated->setExitAccelerationMax(lead->aMaxExit * sq(timeFactor));
                            if (!profile.isConstantAcceleration()) coordinated->setJerkMax(lead->jMax * cube(timeFactor));
                        }
                    }
                    if (coordinated) {
                        coordinated->solve();
                        if (coordinated->m_time <= maxTime && !coordinated->checkValidity()) {
                            profile.copyProfileSolution(*coordinated);
                            continue;
                        }
                    }
                }
                if (profile.v[0] == 0 && profile.v[kSeg] == 0 && profile.a[0] == 0 && profile.a[kSeg] == 0) {
                    // No entry/exit conditions: just re-timed.
                    profile.retimeProfile();
                    continue;
                }
            }
            if (profile.hasOption(SynchronizeEarlyBird) && profile.v[kSeg] == 0 && (profile.isConstantAcceleration() || profile.a[kSeg] == 0)) {
                // Left as it is: the axis gets there early and stays put.
                profile.t[kSeg + 1] += profile.tMin - profile.m_time;
                profile.m_time = profile.tMin;
                profile.computeBounds();
                continue;
            }
            if (profile.hasOption(SynchronizeLastMinute) && profile.v[0] == 0 && (profile.isConstantAcceleration() || profile.a[0] == 0)) {
                profile.t[0] += profile.tMin - profile.m_time;
                profile.m_time = profile.tMin;
                profile.computeBounds();
                continue;
            }
            // None of the simple solutions: solved again with tMin.
            profile.solve();
            if (profile.m_time > maxTime) {
                // At/near entry/exit speeds the new tMin can be impossible: restart with more time.
                maxTime = profile.m_time;
                restart = true;
                break;
            }
        }
    } while (restart);
}

void JPMotionProfile::solveByExpansion(double sgn, bool expandEntry, bool expandExit) {
    computeProfile(sgn * vMax, effectiveEntryVelocity(jMax), effectiveExitVelocity(jMax), tMin);
    if (expandEntry) {
        const double overlap = expandExit ? 0 : sgn * std::max(0.0, sgn * (s[0] - s[4]));
        const double entryExpansion = s[3] - s[0] + overlap;
        for (int i = 0; i <= 3; i++) s[i] -= entryExpansion;
    }
    if (expandExit) {
        const double overlap = expandEntry ? 0 : sgn * std::max(0.0, sgn * (s[3] - s[kSeg]));
        const double exitExpansion = s[kSeg] - s[4] + overlap;
        for (int i = 4; i <= 7; i++) s[i] += exitExpansion;
    }
    computeTime(tMin);
    computeBounds();
    setOption(Solved);
}

void JPMotionProfile::extractProfileSectionFrom(const JPMotionProfile& solved, double t0, double t7) {
    if (t0 == 0 && t7 == solved.m_time) {
        copyProfileSolution(solved);   // the whole of it
        return;
    }
    // A part: t0 and t7 crossing times of s[0] and s[7] on `solved`.
    const double v0 = solved.momentaryVelocity(t0), a0 = solved.momentaryAcceleration(t0);
    const double v7 = solved.momentaryVelocity(t7), a7 = solved.momentaryAcceleration(t7);
    double tSeg = 0, tEntrySlack = 0, tExitSlack = 0;
    for (int seg = 0; seg <= kSeg; seg++) {
        const double tSegEnd = tSeg + solved.t[seg];
        if (tSegEnd <= t0) {
            // Ends before the in cut.
            s[seg] = s[0];
            v[seg] = v0;
            a[seg] = solved.isConstantAcceleration() ? solved.a[seg] : a0;   // jumps
            j[seg] = solved.j[seg];
            t[seg] = 0;
            if (seg == 4) tEntrySlack += solved.t[seg];
        } else if (tSegEnd >= t7) {
            // Ends after the out cut; its beginning and/or end may overlap.
            s[seg] = s[7];
            v[seg] = v7;
            a[seg] = a7;
            t[seg] = std::max(solved.t[seg] - std::max(0.0, t0 - tSeg) - (tSegEnd - t7), 0.0);
            j[seg] = 0;
            if (seg == 4) tExitSlack += solved.t[seg] - t[seg];
        } else {
            // Ends within the cut; only a part of it may be in.
            s[seg] = solved.s[seg];
            v[seg] = solved.v[seg];
            a[seg] = solved.a[seg];
            j[seg] = solved.j[seg];
            t[seg] = solved.t[seg] - std::max(0.0, t0 - tSeg);
            if (seg == 4) tEntrySlack += solved.t[seg] - t[seg];
        }
        tSeg = tSegEnd;
    }
    m_time = t7 - t0;
    computeBounds();
    setOption(Solved);
    if (t0 == 0) {
        if (solved.hasOption(UnconstrainedEntry)) setOption(UnconstrainedEntry);
    } else {
        setOption(CroppedEntry);
        sEntryControl = solved.s[0] + solved.v[4] * tEntrySlack;
        tEntryControl = t0 - tEntrySlack;
    }
    if (t7 == solved.m_time) {
        if (solved.hasOption(UnconstrainedExit)) setOption(UnconstrainedExit);
    } else {
        setOption(CroppedExit);
        sExitControl = solved.s[kSeg] - solved.v[4] * tExitSlack;
        tExitControl = solved.m_time - t7 - tExitSlack;
    }
}

void JPMotionProfile::copyProfileSolution(const JPMotionProfile& from) {
    for (int seg = 0; seg <= kSeg; seg++) {
        s[seg] = from.s[seg];
        v[seg] = from.v[seg];
        a[seg] = from.a[seg];
        j[seg] = from.j[seg];
        t[seg] = from.t[seg];
    }
    m_time = from.m_time;
    sBound0 = from.sBound0;
    sBound1 = from.sBound1;
    tSBound0 = from.tSBound0;
    tSBound1 = from.tSBound1;
    vBound0 = from.vBound0;
    vBound1 = from.vBound1;
    tVBound0 = from.tVBound0;
    tVBound1 = from.tVBound1;
    aBound0 = from.aBound0;
    aBound1 = from.aBound1;
    tABound0 = from.tABound0;
    tABound1 = from.tABound1;
    // Only the flags the solution depends on.
    if (from.hasOption(UnconstrainedEntry)) setOption(UnconstrainedEntry);
    if (from.hasOption(UnconstrainedExit)) setOption(UnconstrainedExit);
    if (from.hasOption(Solved)) setOption(Solved);
}

int JPMotionProfile::leadAxisIndex(const std::vector<double>& vector) {
    double d = 0;
    int lead = 0;
    for (int i = 0; i < int(vector.size()); i++)
        if (std::abs(vector[i]) > d) {
            d = std::abs(vector[i]);
            lead = i;
        }
    return lead;
}

int JPMotionProfile::leadAxisIndex(const std::vector<JPMotionProfile>& profiles) { return leadAxisIndex(unitVector(profiles)); }

double JPMotionProfile::dotProduct(const std::vector<double>& u1, const std::vector<double>& u2) {
    double dot = 0;
    for (size_t i = 0; i < u1.size(); i++) dot += u1[i] * u2[i];
    return dot;
}

std::vector<double> JPMotionProfile::unitVector(const std::vector<JPMotionProfile>& profiles) {
    std::vector<double> u(profiles.size());
    double sumSq = 0;
    for (size_t i = 0; i < profiles.size(); i++) {
        u[i] = profiles[i].s[kSeg] - profiles[i].s[0];
        sumSq += u[i] * u[i];
    }
    const double n = std::sqrt(sumSq);
    for (double& x : u) x = n > 0 ? x / n : 0;
    return u;
}

bool JPMotionProfile::isCoordinated(const std::vector<JPMotionProfile>& profiles) {
    return !profiles.empty() && profiles[0].hasOption(Coordinated);
}

void JPMotionProfile::computeProfile(double vPeak, double vEffEntry, double vEffExit, double tMin_) {
    // Effective entry/exit velocities against vPeak: accelerate or decelerate on entry/exit.
    double signumEntry = signum(vPeak - vEffEntry);
    double signumExit = signum(vPeak - vEffExit);
    if (signumEntry == 0) signumEntry = (signumExit == 0.0 ? 1.0 : signumExit);
    if (signumExit == 0) signumExit = signumEntry;
    const double dVEntry = (vPeak - v[0]);
    const double dVExit = (v[7] - vPeak);
    // No waits before/after.
    t[0] = 0;
    t[kSeg + 1] = 0;

    if (isConstantAcceleration()) {
        // A simple constant acceleration profile: the acceleration jumps right up, so a[0] changes.
        j[0] = 0;
        a[0] = signumEntry * aMaxEntry;
        const double tAccelEntry = dVEntry / a[0];
        t[1] = 0;
        v[1] = v[0];
        s[1] = s[0];

        t[2] = tAccelEntry;
        j[2] = 0;
        a[1] = a[0];
        v[2] = vPeak;
        s[2] = s[1] + v[1] * t[2] + 1. / 2 * a[1] * sq(t[2]);

        t[3] = 0;
        j[3] = 0;
        a[2] = a[1];
        v[3] = vPeak;
        s[3] = s[2];
        a[3] = 0;

        // The exit ramp, reversed.
        j[6] = 0;
        a[6] = -signumExit * aMaxExit;
        const double tAccelExit = dVExit / a[6];
        t[7] = 0;
        v[6] = v[7];
        s[6] = s[7];

        t[6] = tAccelExit;
        j[5] = 0;
        a[5] = a[6];
        v[5] = vPeak;
        s[5] = s[6] - v[6] * t[6] + 1. / 2 * a[6] * sq(t[6]);

        t[5] = 0;
        j[4] = 0;
        a[4] = a[5];
        v[4] = vPeak;
        s[4] = s[5];
    } else if (hasOption(SimplifiedSCurve)) {
        // The acceleration limited on very short moves, then jerk for the same average constant acceleration.
        double aMaxEn = aMaxEntry;
        if (std::abs(dVEntry) < kEps) {
            aMaxEn = 0;
            j[0] = signumEntry * jMax;
        } else {
            // For very low velocity deltas the acceleration must be limited, lest jMax be violated.
            if (signumEntry * aMaxEn * aMaxEn / dVEntry > jMax) aMaxEn = std::sqrt(std::abs(dVEntry * jMax));
            j[0] = aMaxEn * aMaxEn / dVEntry;
        }
        a[0] = 0;   // nothing else is supported

        a[1] = signumEntry * aMaxEn;
        t[1] = a[1] / j[0];
        s[1] = s[0] + v[0] * t[1] + 1. / 6 * j[0] * cube(t[1]);
        v[1] = v[0] + 1. / 2 * j[0] * sq(t[1]);

        j[1] = 0;
        t[2] = 0;
        a[2] = a[1];
        v[2] = v[1];
        s[2] = s[1];

        j[2] = -j[0];
        t[3] = t[1];
        a[3] = 0;
        v[3] = vPeak;
        s[3] = s[2] + v[2] * t[3] + 1. / 2 * a[2] * sq(t[3]) + 1. / 6 * j[2] * cube(t[3]);

        // The exit ramp, reversed.
        double aMaxEx = aMaxExit;
        if (std::abs(dVExit) < kEps) {
            aMaxEx = 0;
            j[6] = signumExit * jMax;
        } else {
            if (-signumExit * aMaxEx * aMaxEx / dVExit > jMax) aMaxEx = std::sqrt(std::abs(dVExit * jMax));
            j[6] = -aMaxEx * aMaxEx / dVExit;
        }
        a[7] = 0;   // nothing else is supported
        a[6] = -signumExit * aMaxEx;
        t[7] = -a[6] / j[6];
        v[6] = v[7] + 1. / 2 * j[6] * sq(t[7]);
        s[6] = s[7] - v[7] * t[7] - 1. / 6 * j[6] * cube(t[7]);

        j[5] = 0;
        t[6] = 0;
        a[5] = a[6];
        v[5] = v[6];
        s[5] = s[6];

        j[4] = -j[6];
        t[5] = t[7];
        a[4] = 0;
        v[4] = vPeak;
        s[4] = s[5] - v[5] * t[5] + 1. / 2 * a[5] * sq(t[5]) - 1. / 6 * j[4] * cube(t[5]);
    } else {
        // A 3rd order profile.
        if (!hasOption(UnconstrainedEntry)) {
            // With a constant acceleration segment, or not (too slow to reach the acceleration limit: an S-curve,
            // straight from positive to negative jerk). The entry acceleration first taken away: as if accelerating
            // from a = 0, t[1] moved backward/forward in time.
            const double j0 = signumEntry * jMax;
            const double t0early = a[0] / j0;
            const double v0early = v[0] - a[0] * t0early + 1. / 2 * j0 * sq(t0early);
            // Half the time to reach the velocity with an S-curve, and the acceleration at its inflection.
            const double t1 = std::sqrt(std::max(0.0, (vPeak - v0early) / j0));
            const double a1 = t1 * j0;
            double aMaxEn = aMaxEntry;
            if (std::abs(a1) < aMaxEn) aMaxEn = std::abs(a1);   // an S curve

            // Phase 1: jerk to acceleration.
            j[0] = j0;
            t[1] = std::max(0.0, signumEntry * aMaxEn / j0 - t0early);   // can be cropped by tearly
            a[1] = a[0] + j[0] * t[1];
            s[1] = s[0] + v[0] * t[1] + 1. / 2 * a[0] * sq(t[1]) + 1. / 6 * j[0] * cube(t[1]);
            v[1] = v[0] + a[0] * t[1] + 1. / 2 * j[0] * sq(t[1]);

            // Phase 2: constant acceleration.
            a[2] = a[1];
            j[1] = 0;
            j[2] = -j0;                    // phase 3 look-ahead
            t[3] = (0 - a[2]) / j[2];      // phase 3 look-ahead
            v[2] = vPeak + 1. / 2 * j[2] * sq(t[3]);
            t[2] = (a[2] == 0 ? 0.0 : (v[2] - v[1]) / a[2]);
            s[2] = s[1] + v[1] * t[2] + 1. / 2 * a[1] * sq(t[2]);

            // Phase 3: negative jerk to constant velocity, zero acceleration.
            v[3] = vPeak;
            a[3] = 0;
            s[3] = s[2] + v[2] * t[3] + 1. / 2 * a[2] * sq(t[3]) + 1. / 6 * j[2] * cube(t[3]);
        }

        // Phase 4: constant velocity (s and t postponed).
        j[3] = 0;

        if (!hasOption(UnconstrainedExit)) {
            v[4] = vPeak;
            a[4] = 0;
            // As on entry: pretend to decelerate to a = 0, t[7] moved forward/backward in time.
            const double j6 = signumExit * jMax;
            const double t7late = -a[7] / j6;
            const double v7late = v[7] + a[7] * t7late + 1. / 2 * j6 * sq(t7late);
            const double t6 = std::sqrt(std::max(0.0, (vPeak - v7late) / j6));
            const double a6 = -t6 * j6;
            double aMaxEx = aMaxExit;
            if (std::abs(a6) < aMaxEx) aMaxEx = std::abs(a6);   // an S curve

            // Phase 5: negative jerk to deceleration.
            j[4] = -j6;
            j[6] = j6;                                              // phase 7 look-ahead
            t[7] = std::max(0.0, signumExit * aMaxEx / j[6] - t7late);   // can be cropped by tlate
            a[6] = a[7] - j[6] * t[7];                              // phase 6 look-ahead

            a[5] = a[6];
            t[5] = a[5] / j[4];
            v[5] = v[4] + 1. / 2 * j[4] * sq(t[5]);

            // Phase 6: constant deceleration.
            j[5] = 0;
            v[6] = v[7] - a[7] * t[7] + 1. / 2 * j[6] * sq(t[7]);
            t[6] = (a[6] == 0 ? 0.0 : (v[6] - v[5]) / a[6]);

            // Phase 7: jerk to the exit acceleration (looked ahead); s backwards.
            s[6] = s[7] - v[7] * t[7] + 1. / 2 * a[7] * sq(t[7]) - 1. / 6 * j[6] * cube(t[7]);
            s[5] = s[6] - v[6] * t[6] + 1. / 2 * a[6] * sq(t[6]);
            s[4] = s[5] - v[5] * t[5] + 1. / 2 * a[5] * sq(t[5]) - 1. / 6 * j[4] * cube(t[5]);
        }
    }
    // Unconstrained half-sided: accelerates towards the other end and cruises freely through it.
    if (hasOption(UnconstrainedEntry)) {
        for (int i = 1; i <= 3; i++) {
            s[i] = s[0];
            v[i] = v[4];
            a[i] = 0;
            j[i] = 0;
            t[i] = 0;
        }
        v[0] = v[4];
        a[0] = 0;
        j[0] = 0;
    }
    if (hasOption(UnconstrainedExit)) {
        s[4] = s[7];
        v[4] = v[3];
        a[4] = 0;
        j[4] = 0;
        for (int i = 5; i <= 7; i++) {
            if (i < 7) s[i] = s[7];
            v[i] = v[3];
            a[i] = 0;
            j[i] = 0;
            t[i] = 0;
        }
    }

    if (tMin_ > 0 && v[4] == 0.0 && s[4] != s[3] && std::abs(s[4] - s[3]) < kVtol * 5 * tMin_ && !hasOption(SimplifiedSCurve)) {
        // A minimum time, zero velocity move whose ramps almost but not quite meet: mended by capping the
        // entry/exit acceleration.
        if (j[0] != 0 && j[1] == 0) {
            // Third order: by symmetry the average velocity gives the lengthening dt.
            const double t2 = t[2];
            const double vm = v[1] + a[1] * t2 * 0.5;
            const double dt = (s[4] - s[3]) / vm;
            if (dt > 0 && dt < tMin_) {
                const double js = j[0], as = a[2];
                // Sage: the time t of the capped acceleration.
                const double sqrtTerm = std::sqrt(sq(dt) * sq(js) + 2 * dt * sq(js) * t2 + sq(js) * sq(t2) + 4 * as * dt * js);
                for (double ts : { -(dt * js + sqrtTerm) / js, -(dt * js - sqrtTerm) / js }) {
                    const double dth = (ts - t2) / 2;
                    if (dth > 0 && dth < t[1] + kEps && dth < t[3] + kEps) {
                        // Phase 1.
                        t[1] -= dth;
                        a[1] = a[0] + j[0] * t[1];
                        v[1] = v[0] + a[0] * t[1] + 1. / 2 * j[0] * sq(t[1]);
                        s[1] = s[0] + v[0] * t[1] + 1. / 2 * a[0] * sq(t[1]) + 1. / 6 * j[0] * cube(t[1]);
                        // Phase 3 backward.
                        t[3] -= dth;
                        s[3] = s[4];   // mend it
                        a[2] = a[3] - j[2] * t[3];
                        v[2] = v[3] - a[3] * t[3] + 1. / 2 * j[2] * sq(t[3]);
                        s[2] = s[3] - v[3] * t[3] + 1. / 2 * a[3] * sq(t[3]) - 1. / 6 * j[2] * cube(t[3]);
                        t[2] = (v[2] - v[1]) / a[1];   // time to mend
                    }
                }
            }
        } else if (isConstantAcceleration()) {
            double ts = t[1] + t[2] + t[3];
            if (ts > kEps) {
                const double vm = (s[3] - s[0]) / ts;
                const double dt = (s[4] - s[3]) / vm;
                if (dt > 0 && dt < tMin_) {
                    ts += dt;
                    const double as = (v[3] - v[0]) / ts;
                    a[0] = as;
                    t[1] = 0;
                    a[1] = as;
                    v[1] = v[0];
                    s[1] = s[0];
                    t[2] = ts;
                    a[2] = as;
                    v[2] = v[3];
                    s[2] = s[4];   // mend it
                    t[3] = 0;
                    a[3] = 0;
                    s[3] = s[4];   // mend it
                }
            }
        }
        if (s[4] != s[3]) {
            // Not mended on the entry ramp: the exit ramp.
            if (j[6] != 0 && j[5] == 0) {
                const double t6 = t[6];
                const double vm = v[5] + a[5] * t6 * 0.5;
                const double dt = (s[4] - s[3]) / vm;
                if (dt > 0 && dt < tMin_) {
                    const double js = j[6], as = -a[6];
                    const double sqrtTerm = std::sqrt(sq(dt) * sq(js) + 2 * dt * sq(js) * t6 + sq(js) * sq(t6) + 4 * as * dt * js);
                    for (double ts : { -(dt * js + sqrtTerm) / js, -(dt * js - sqrtTerm) / js }) {
                        const double dth = (ts - t6) / 2;
                        if (dth > 0 && dth < t[7] + kEps && dth < t[5] + kEps) {
                            // Phase 7 backward.
                            t[7] -= dth;
                            a[6] = a[7] - j[6] * t[7];
                            v[6] = v[7] - a[7] * t[7] + 1. / 2 * j[6] * sq(t[7]);
                            s[6] = s[7] - v[7] * t[7] + 1. / 2 * a[7] * sq(t[7]) - 1. / 6 * j[6] * cube(t[7]);
                            // Phase 5.
                            t[5] -= dth;
                            s[4] = s[3];   // mend it
                            a[5] = a[4] + j[4] * t[5];
                            v[5] = v[4] + a[4] * t[5] + 1. / 2 * j[4] * sq(t[5]);
                            s[5] = s[4] + v[4] * t[5] + 1. / 2 * a[4] * sq(t[5]) + 1. / 6 * j[4] * cube(t[5]);
                            t[6] = (v[6] - v[5]) / a[5];   // time to mend
                        }
                    }
                }
            } else if (isConstantAcceleration()) {
                double ts = t[7] + t[6] + t[5];
                if (ts > kEps) {
                    const double vm = (s[7] - s[4]) / ts;
                    const double dt = (s[4] - s[3]) / vm;
                    if (dt > 0 && dt < tMin_) {
                        ts += dt;
                        const double as = (v[7] - v[4]) / ts;
                        t[7] = 0;
                        a[6] = as;
                        v[6] = v[7];
                        s[6] = s[7];
                        t[6] = ts;
                        a[5] = as;
                        v[5] = v[4];
                        s[5] = s[3];   // mend it
                        t[5] = 0;
                        a[4] = as;
                        s[4] = s[3];   // mend it
                    }
                }
            }
        }
    }
    if (!(hasOption(UnconstrainedEntry) || hasOption(UnconstrainedExit) || hasOption(SimplifiedSCurve) || isConstantAcceleration())) {
        // A regular 3rd order profile.
        if (signum(j[2]) != signum(j[4])) {
            // A sign reversal ramp: both accelerate or both decelerate.
            double tOverlap = 0;
            if (v[4] == 0.0 && std::abs(s[3] - s[4]) < kEps) {
                // Velocity and s difference zero: the whole jerk time taken up.
                t[4] = 0;
                tOverlap = std::min(t[3], t[5]);
                if (tMin_ > 0) {
                    m_time = std::accumulate(t.begin(), t.end(), 0.0);
                    tOverlap = std::min(tOverlap, m_time - tMin_);   // restricted to the minimum time violation
                }
            } else {
                // Velocity not zero: the s overlap time.
                t[4] = (s[4] - s[3]) / v[4];
                tOverlap = -t[4];
            }
            if (tOverlap > 0 && std::isfinite(tOverlap)) {
                // s overlaps in the middle (the profile invalid), as the jerk phases there work against each other
                // in sign reversal ramps: fused into a constant acceleration segment. By symmetry the time at
                // constant acceleration is half that of the two jerk phases, so tOverlap comes off both.
                if (t[3] + kEps > tOverlap && t[5] + kEps > tOverlap) {
                    t[3] -= tOverlap;
                    j[3] = 0;
                    a[3] = a[2] + j[2] * t[3];
                    v[3] = v[2] + a[2] * t[3] + 1. / 2 * j[2] * sq(t[3]);
                    s[3] = s[2] + v[2] * t[3] + 1. / 2 * a[2] * sq(t[3]) + 1. / 6 * j[2] * cube(t[3]);

                    t[4] = tOverlap;
                    a[4] = a[3];
                    v[4] = v[3] + a[3] * t[4];
                    s[4] = s[3] + v[3] * t[4] + 1. / 2 * a[3] * sq(t[4]);

                    t[5] -= tOverlap;
                }
            }
        }
    }
    computeTime(tMin_);
    computeBounds();
    m_eval++;
}

void JPMotionProfile::computeTime(double tMin_) {
    bool adjustMinTime = false;
    if (a[3] == 0) {
        // Not fused: the cruising time.
        if (v[4] == 0.0 && std::abs(s[3] - s[4]) < kEps) {
            t[4] = 0;
            adjustMinTime = true;
        } else {
            t[4] = (s[4] - s[3]) / v[4];
        }
    }
    m_time = std::accumulate(t.begin(), t.end(), 0.0);
    if (adjustMinTime && tMin_ > m_time) {
        // Zero velocity: the minimum time taken directly.
        t[4] = tMin_ - m_time;
        m_time = tMin_;
    }
}

std::optional<double> JPMotionProfile::forwardCrossingTime(double sCross, bool halfProfile) const {
    double tSeg = t[0];
    if (halfProfile) {
        const double sgn = signum(v[4]);
        if (sgn * sCross >= sgn * s[3]) {
            for (int i = 1; i <= 3; i++) tSeg += t[i];
            return tSeg;
        }
    }
    for (int i = 1; i <= (halfProfile ? 3 : kSeg); i++) {
        if (const auto ts = segmentCrossingTime(sCross, tSeg, i, true)) return ts;
        tSeg += t[i];
    }
    return std::nullopt;
}

std::optional<double> JPMotionProfile::backwardCrossingTime(double sCross, bool halfProfile) const {
    double tSeg = m_time - t[kSeg + 1];
    if (halfProfile) {
        const double sgn = signum(v[4]);
        if (sgn * sCross <= sgn * s[4]) {
            for (int i = kSeg; i > 4; i--) tSeg -= t[i];
            return tSeg;
        }
    }
    for (int i = kSeg; i >= (halfProfile ? 4 : 1); i--) {
        tSeg -= t[i];
        if (const auto ts = segmentCrossingTime(sCross, tSeg, i, false)) return ts;
    }
    return std::nullopt;
}

std::optional<double> JPMotionProfile::segmentCrossingTime(double sCross, double tSeg, int i, bool forward) const {
    const double ti = t[i];
    const double jerk = j[i - 1];
    if (!forward && std::abs(s[i] - sCross) < kEps) return i == kSeg ? m_time : tSeg + ti;
    if (std::abs(s[i - 1] - sCross) < kEps) return i == 1 ? 0.0 : tSeg;
    if (forward && std::abs(s[i] - sCross) < kEps) return i == kSeg ? m_time : tSeg + ti;
    if (jerk != 0) {
        // With jerk: the velocity's roots first (the path's possible extremes) bound intervals to find the third
        // order roots in, numerically.
        const double s0 = s[i - 1], ds = sCross - s0, v0 = v[i - 1], a0 = a[i - 1];
        double sqArg = a0 * a0 - 2 * jerk * v0;
        if (sqArg < 0 && sqArg > -kEps) sqArg = 0;
        const double sTerm = std::sqrt(sqArg);
        const double ti0 = 0;
        double ti1 = std::max(ti0, std::min(ti, -(a0 + sTerm) / jerk));
        double ti2 = std::max(ti0, std::min(ti, -(a0 - sTerm) / jerk));
        auto f = [&](double x) { return -ds + v0 * x + 1. / 2 * a0 * sq(x) + 1. / 6 * jerk * cube(x); };
        auto g = [&](double x) { return v0 + a0 * x + 1. / 2 * jerk * sq(x); };
        if (ti1 > ti2) std::swap(ti1, ti2);
        // The first (forward) or the last in time.
        const double intervals[3][2] = { { forward ? ti0 : ti2, forward ? ti1 : ti }, { ti1, ti2 }, { forward ? ti2 : ti0, forward ? ti : ti1 } };
        for (const auto& in : intervals)
            if (in[0] < in[1])
                if (const auto ts = newtonSolve(in[0], in[1], f, g, true)) return *ts + tSeg;
    } else if (a[i - 1] != 0) {
        // With acceleration: t == -(v0 +- sqrt(2*a0*s + v0^2))/a0.
        const double ds = sCross - s[i - 1], v0 = v[i - 1], a0 = a[i - 1];
        const double rTerm = 2 * a0 * ds + v0 * v0;
        if (rTerm >= 0) {
            const double sTerm = std::sqrt(rTerm);
            double ts1 = -(v0 + sTerm) / a0, ts2 = -(v0 - sTerm) / a0;
            if ((ts1 > ts2) ^ forward) std::swap(ts1, ts2);   // the first (forward) or second in time
            if (ts1 >= 0 && ts1 < ti) return ts1 + tSeg;
            if (ts2 >= 0 && ts2 < ti) return ts2 + tSeg;
        }
    } else if (v[i - 1] != 0) {
        // Just velocity.
        const double ts = (sCross - s[i - 1]) / v[i - 1];
        if (ts > 0 && ts < ti) return ts + tSeg;
    }
    return std::nullopt;
}

void JPMotionProfile::computeBounds() {
    if (s[0] <= s[kSeg]) {
        sBound0 = s[0];
        tSBound0 = 0;
        sBound1 = s[kSeg];
        tSBound1 = m_time;
    } else {
        sBound0 = s[kSeg];
        tSBound0 = m_time;
        sBound1 = s[0];
        tSBound1 = 0;
    }
    vBound0 = vBound1 = v[0];
    tVBound0 = tVBound1 = 0;
    aBound0 = aBound1 = a[0];
    tABound0 = tABound1 = 0;
    double tSeg = t[0];
    for (int i = 1; i <= kSeg; i++) {
        // The velocity crossing zero may be an extreme of s, the acceleration crossing zero one of v.
        if (j[i - 1] != 0) {
            // A 3rd order segment.
            const double dt = std::sqrt(sq(a[i - 1]) - 2 * v[i - 1] * j[i - 1]);
            for (double tCross : { -(a[i - 1] + dt) / j[i - 1], -(a[i - 1] - dt) / j[i - 1] })
                if (tCross >= 0 && tCross <= t[i]) {
                    const double sExtreme = s[i - 1] + v[i - 1] * tCross + 1. / 2 * a[i - 1] * sq(tCross) + 1. / 6 * j[i - 1] * cube(tCross);
                    if (sExtreme < sBound0) {
                        sBound0 = sExtreme;
                        tSBound0 = tSeg + tCross;
                    }
                    if (sExtreme > sBound1) {
                        sBound1 = sExtreme;
                        tSBound1 = tSeg + tCross;
                    }
                }
            const double tCross = -a[i - 1] / j[i - 1];
            if (tCross >= 0 && tCross <= t[i]) {
                const double vExtreme = v[i - 1] + a[i - 1] * tCross + 1. / 2 * j[i - 1] * sq(tCross);
                if (vExtreme < vBound0) {
                    vBound0 = vExtreme;
                    tVBound0 = tSeg + tCross;
                }
                if (vExtreme > vBound1) {
                    vBound1 = vExtreme;
                    tVBound1 = tSeg + tCross;
                }
            }
        } else if (a[i] != 0) {
            // A 2nd order segment.
            const double tCross = -v[i - 1] / a[i - 1];
            if (tCross >= 0 && tCross <= t[i]) {
                const double sExtreme = s[i - 1] + v[i - 1] * tCross + 1. / 2 * a[i - 1] * sq(tCross);
                if (sExtreme < sBound0) {
                    sBound0 = sExtreme;
                    tSBound0 = tSeg + tCross;
                }
                if (sExtreme > sBound1) {
                    sBound1 = sExtreme;
                    tSBound1 = tSeg + tCross;
                }
            }
        }
        tSeg += t[i];
        // The node extremes too, in case they were missed numerically.
        if (s[i] < sBound0) {
            sBound0 = s[i];
            tSBound0 = tSeg;
        }
        if (s[i] > sBound1) {
            sBound1 = s[i];
            tSBound1 = tSeg;
        }
        if (v[i] < vBound0) {
            vBound0 = v[i];
            tVBound0 = tSeg;
        }
        if (v[i] > vBound1) {
            vBound1 = v[i];
            tVBound1 = tSeg;
        }
        if (a[i] < aBound0) {
            aBound0 = a[i];
            tABound0 = tSeg;
        }
        if (a[i] > aBound1) {
            aBound1 = a[i];
            tABound1 = tSeg;
        }
    }
}

double JPMotionProfile::effectiveEntryVelocity(double jMax_) const {
    if (hasOption(UnconstrainedEntry)) return 0.0;
    if (isConstantAcceleration()) return v[0];
    const double jEntry = (a[0] == 0 ? 1.0 : -signum(a[0])) * jMax_;
    const double tEntry = -a[0] / jEntry;
    return v[0] + a[0] * tEntry + 1. / 2 * jEntry * sq(tEntry);
}

double JPMotionProfile::effectiveExitVelocity(double jMax_) const {
    if (hasOption(UnconstrainedExit)) return 0.0;
    if (isConstantAcceleration()) return v[kSeg];
    const double jExit = (a[kSeg] == 0 ? 1.0 : signum(a[kSeg])) * jMax_;
    const double tExit = -a[kSeg] / jExit;
    return v[kSeg] + a[kSeg] * tExit + 1. / 2 * jExit * sq(tExit);
}

} // inline namespace jf
