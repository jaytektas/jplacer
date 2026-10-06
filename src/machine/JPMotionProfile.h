// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPMotionControlType.h"

#include <array>
#include <optional>
#include <vector>

inline namespace jf {

// OpenPnP's MotionProfile (Mark's 3rd order motion profiles, 2020): one axis's motion from s0 to s7 as seven
// segments of constant jerk (jerk up, constant acceleration, jerk down, cruise, jerk down, constant deceleration,
// jerk up), entering at v0, a0 and leaving at v7, a7, within the location, velocity, acceleration and jerk limits
// and at least tMin long. A jerk limit of 0 (or infinite) gives a constant acceleration profile. solve() finds the
// fastest such profile numerically, as OpenPnP's does: its regions, root finding and analytical guesses alike.
// Several axes' profiles are coordinated (straight line, the lead axis's profile scaled) or synchronized (each
// its own, all taking the same time). Distances in mm (or degrees), times in seconds.
class JPMotionProfile {
public:
    static constexpr int kSegments = 7;

    // OpenPnP's ProfileOption, as bits.
    enum Option {
        Coordinated,
        SynchronizeEarlyBird,
        SynchronizeLastMinute,
        SynchronizeStraighten,
        Jog,
        RestrictToCoordinated,
        SimplifiedSCurve,
        UnconstrainedExit,
        UnconstrainedEntry,
        CroppedEntry,
        CroppedExit,
        Solved,
    };
    static constexpr int flag(Option o) { return 1 << int(o); }

    // OpenPnP's ErrorState, in descending order of severity.
    enum class Error {
        SolutionNotFinite,
        NegativeSegmentTime,
        TimeSumMismatch,
        LocationDiscontinuity,
        VelocityDiscontinuity,
        AccelerationDiscontinuity,
        MinTimeViolated,
        MaxTimeViolated,
        MinLocationViolated,
        MaxLocationViolated,
        MaxVelocityViolated,
        MaxAccelerationViolated,
        MaxJerkViolated,
    };

    JPMotionProfile() = default;
    JPMotionProfile(double s0, double s1, double v0, double v1, double a0, double a1, double sMin, double sMax, double vMax,
                    double aMaxEntry, double aMaxExit, double jMax, double tMin, double tMax, int options);
    // As OpenPnP's copy constructor: the constraints and entry/exit conditions, not the solution.
    static JPMotionProfile like(const JPMotionProfile& t);

    bool hasOption(Option o) const { return (m_options & flag(o)) != 0; }
    void setOption(Option o) { m_options |= flag(o); }
    void clearOption(Option o) { m_options &= ~flag(o); }
    int  options() const { return m_options; }

    double location(int segment) const { return s[segment]; }
    double velocity(int segment) const { return v[segment]; }
    double acceleration(int segment) const { return a[segment]; }
    double jerk(int segment) const { return j[segment]; }
    double segmentTime(int segment) const { return t[segment]; }   // 0 and kSegments+1: waits before and after
    double segmentBeginTime(int segment) const;

    double locationMin() const { return sMin; }
    double locationMax() const { return sMax; }
    double velocityMax() const { return vMax; }
    double entryAccelerationMax() const { return aMaxEntry; }
    double exitAccelerationMax() const { return aMaxExit; }
    double accelerationMax() const { return aMaxEntry > aMaxExit ? aMaxEntry : aMaxExit; }
    double jerkMax() const { return jMax; }
    double timeMin() const { return tMin; }
    double timeMax() const { return tMax; }
    void setLocationMin(double x) { sMin = x; }
    void setLocationMax(double x) { sMax = x; }
    void setVelocityMax(double x) { vMax = x; }
    void setEntryAccelerationMax(double x) { aMaxEntry = x; }
    void setExitAccelerationMax(double x) { aMaxExit = x; }
    void setJerkMax(double x) { jMax = x; }
    void setTimeMin(double x) { tMin = x; }
    void setTimeMax(double x) { tMax = x; }

    // The extremes reached and when.
    double lowerSBoundary() const { return sBound0; }
    double higherSBoundary() const { return sBound1; }
    double lowerVBoundary() const { return vBound0; }
    double higherVBoundary() const { return vBound1; }
    double lowerABoundary() const { return aBound0; }
    double higherABoundary() const { return aBound1; }
    double lowerSBoundaryTime() const { return tSBound0; }
    double higherSBoundaryTime() const { return tSBound1; }
    double lowerVBoundaryTime() const { return tVBound0; }
    double higherVBoundaryTime() const { return tVBound1; }

    bool isSolved() const { return hasOption(Solved); }
    bool isEmpty() const;
    bool isConstantAcceleration() const;
    bool isSupportingUncoordinated() const { return !(hasOption(SimplifiedSCurve) || hasOption(RestrictToCoordinated)); }

    // What a controller of `type` is told for this profile: its limits (EuclideanAxisLimits) or what it reaches.
    double profileVelocity(JPMotionControlType type) const;
    double profileAcceleration(JPMotionControlType type) const;
    double profileJerk(JPMotionControlType type) const;

    double time() const { return m_time; }
    double momentaryLocation(double time) const;
    double momentaryVelocity(double time) const;
    double momentaryAcceleration(double time) const;
    double momentaryJerk(double time) const;

    // None: consistent, coordinated, safe and constrained.
    std::optional<Error> checkValidity() const;

    void solve();
    void solve(int iterations, double vtol, double ttol);
    bool solveForVelocity(int iterations, double vtol, double ttol);
    bool solveIfNullMove();
    bool retimeProfile();
    bool assertSolved();
    void solveByExpansion(double signum, bool expandEntry, bool expandExit);
    void computeProfile(double vPeak, double vEffEntry, double vEffExit, double tMin);
    void computeBounds();

    static void coordinateProfiles(std::vector<JPMotionProfile>& profiles);
    static void coordinateProfilesToLead(std::vector<JPMotionProfile>& profiles, const JPMotionProfile& lead);
    void coordinateProfileToLead(const JPMotionProfile& lead);
    static void synchronizeProfiles(std::vector<JPMotionProfile>& profiles);

    void extractProfileSectionFrom(const JPMotionProfile& solved, double t0, double t7);
    void copyProfileSolution(const JPMotionProfile& from);

    static int leadAxisIndex(const std::vector<double>& vector);
    static int leadAxisIndex(const std::vector<JPMotionProfile>& profiles);
    static double dotProduct(const std::vector<double>& u1, const std::vector<double>& u2);
    static std::vector<double> unitVector(const std::vector<JPMotionProfile>& profiles);
    static bool isCoordinated(const std::vector<JPMotionProfile>& profiles);

    std::optional<double> forwardCrossingTime(double sCross, bool halfProfile) const;
    std::optional<double> backwardCrossingTime(double sCross, bool halfProfile) const;

    double effectiveEntryVelocity(double jMax) const;
    double effectiveExitVelocity(double jMax) const;

    // A section extracted from a longer profile: where its control point lies outside it (CroppedEntry/Exit).
    double entryControlLocation() const { return sEntryControl; }
    double exitControlLocation() const { return sExitControl; }
    double entryControlTime() const { return tEntryControl; }
    double exitControlTime() const { return tExitControl; }

    // OpenPnP's solver constants.
    static constexpr int    kIterations = 80;
    static constexpr double kVtol = 2.0;            // mm/s
    static constexpr double kAtol = kVtol * 2;      // mm/s^2
    static constexpr double kJtol = kAtol * 4;      // mm/s^3
    static constexpr double kTtol = 0.000001;       // s
    static constexpr double kEps = 1e-8;

private:
    bool solveRegion(double vPeak0, double vPeak1, double sResult0, double sResult1, double tResult0, double tResult1,
                     double vEffEntry, double vEffExit, double tMin, double bestTime, int iterations, double stol, double vtol,
                     double ttol);
    double profileSignum(double vEffEntry, double vEffExit) const;
    void computeTime(double tMin);
    std::optional<double> segmentCrossingTime(double sCross, double tSeg, int i, bool forward) const;

    std::array<double, kSegments + 1> s {}, a {}, v {}, j {};
    std::array<double, kSegments + 2> t {};   // one more: the wait before (t[0]) and after a synchronized move
    double sMin = 0, sMax = 0, vMax = 0, aMaxEntry = 0, aMaxExit = 0, jMax = 0, tMin = 0, tMax = 0;
    double sEntryControl = 0, sExitControl = 0, tEntryControl = 0, tExitControl = 0;
    int    m_eval = 0;
    double m_time = 0;
    double sBound0 = 0, sBound1 = 0, tSBound0 = 0, tSBound1 = 0;
    double vBound0 = 0, tVBound0 = 0, vBound1 = 0, tVBound1 = 0;
    double aBound0 = 0, tABound0 = 0, aBound1 = 0, tABound1 = 0;
    int    m_options = 0;
};

} // inline namespace jf
