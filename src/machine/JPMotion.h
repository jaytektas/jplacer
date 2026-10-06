// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPMotionControlType.h"
#include "JPMotionProfile.h"

#include <map>
#include <optional>
#include <string>
#include <vector>

inline namespace jf {

// OpenPnP's Motion: one move of several axes from location0 to location1, planned as each axis's motion profile
// (JPMotionProfile) and turned into what the controllers are sent, as their Motion Control Type says. Coordinated
// (the default): a straight line, the axes' limits applied to the whole move as OpenPnP does (per unit of the
// Euclidean move, and the feed rate over the linear axes as G-code reads F, NIST RS274NGC 2.1.2.5). Uncoordinated:
// each axis within its own limits (and the safe zone, where asked), all taking the same time. Locations by axis id,
// in mm (degrees for rotation).
class JPMotion {
public:
    // An axis as the motion sees it: its limits per second (OpenPnP's getMotionLimit: velocity, acceleration,
    // jerk; the jerk 0 when its controller has constant acceleration), how fine its steps are, and its soft limits
    // and safe zone.
    struct Axis {
        std::string id, driverId;
        bool   rotational = false;   // rotational on its controller
        double limit[4] {};          // [1] velocity, [2] acceleration, [3] jerk; 0: none
        double resolution = kDefaultResolution;
        std::optional<double> softLow, softHigh, safeLow, safeHigh;
        // Two coordinates the same, as the axis can tell (in its resolution ticks).
        bool matches(double a, double b) const;
    };
    static constexpr double kDefaultResolution = 0.0001;   // OpenPnP's

    // A controller as the motion sees it.
    struct Driver {
        std::string         id;
        JPMotionControlType type;
        double              feedRatePerSecond = 0;   // its Max. Feed Rate (mm/s; 0: none)
        double              minimumRate[4] { 1, 1, 1, 1 };   // OpenPnP's getMinimumRate, mm/s^order
        // OpenPnP's GcodeAsyncDriver interpolation (Simulated3rdOrderControl).
        int    interpolationMaxSteps = 32, interpolationJerkSteps = 4, interpolationMinStep = 16;
        double interpolationTimeStep = 0.001, junctionDeviation = 0.02;
    };

    // OpenPnP's MotionOption, as bits.
    enum Option {
        SpeedOverPrecision,
        UncoordinatedMotion,
        SynchronizeStraighten,
        SynchronizeEarlyBird,
        SynchronizeLastMinute,
        NoDriverLimit,
        LimitToSafeZone,
        JogMotion,
        Stillstand,
        InterpolationFailed,
    };
    static constexpr int flag(Option o) { return 1 << int(o); }

    using Location = std::map<std::string, double>;

    // OpenPnP's MoveToCommand: the waypoint, the axes that move to it, and the rates the controller is told (mm and
    // seconds; none: not set), with when it starts and how long it takes, and its entry and exit velocity.
    struct MoveTo {
        Location location0, location1, moved;
        std::optional<double> feedRatePerSecond, accelerationPerSecond2, jerkPerSecond3;
        double t0 = 0;
        std::optional<double> time, v0, v1;
    };

    // `axes`: every axis of the move (those not in `location1` stay where `location0` has them). The overrides (0:
    // none) cap the rates as a move's own speed would (OpenPnP's feedrate/acceleration/jerk overrides).
    JPMotion(std::vector<Axis> axes, const std::vector<Driver>& drivers, Location location0, Location location1, double nominalSpeed,
             int options, double feedrateOverride = 0, double accelerationOverride = 0, double jerkOverride = 0);

    bool hasOption(Option o) const { return (m_options & flag(o)) != 0; }
    void setOption(Option o) { m_options |= flag(o); }
    int  options() const { return m_options; }

    double time() const { return m_profiles.empty() ? 0 : m_profiles[0].time(); }
    double nominalSpeed() const { return m_nominalSpeed; }
    double effectiveSpeed() const { return m_effectiveSpeed; }
    double euclideanDistance() const { return m_euclideanDistance; }
    bool   isEmpty() const { return m_euclideanDistance == 0 && time() == 0; }
    const Location& location0() const { return m_location0; }
    const Location& location1() const { return m_location1; }
    const std::vector<Axis>& axes() const { return m_axes; }
    const std::vector<JPMotionProfile>& profiles() const { return m_profiles; }
    std::vector<JPMotionProfile>& profiles() { return m_profiles; }   // for JPMotionPath's optimizing
    const std::vector<Driver>& drivers() const { return m_drivers; }

    Location momentaryLocation(double t) const;
    Location momentaryVelocity(double t) const;
    Location momentaryAcceleration(double t) const;
    Location momentaryJerk(double t) const;

    // The target with only the given controller's moving axes in it.
    Location movingAxesTarget(const std::string& driverId) const;

    // The rates over the controller's axes as G-code reads them (NIST RS274NGC 2.1.2.5: over the linear axes, or
    // the rotational ones when nothing linear moves), mm per second^order; none where its type does not set them.
    std::optional<double> feedRatePerSecond(const Driver& d) const;
    std::optional<double> accelerationPerSecond2(const Driver& d) const;
    std::optional<double> jerkPerSecond3(const Driver& d) const;

    // What the controller is sent for this motion, as its type says: one move (its rates as planned or its limits),
    // one moderated constant acceleration move (ModeratedConstantAcceleration), or the move interpolated into
    // constant acceleration steps simulating jerk control (Simulated3rdOrderControl; `retiming`: stretched to the
    // planned time). Interpolation failing (too many steps) gives one moderated move, InterpolationFailed set.
    std::vector<MoveTo> interpolatedMoveToCommands(const Driver& d, bool retiming);
    std::vector<MoveTo> moderatedMoveTo(const Driver& d) const;
    std::vector<MoveTo> singleMoveTo(const Driver& d) const;

private:
    void computeLimitsAndProfile(const std::vector<Driver>& drivers, double feedrateOverride, double accelerationOverride,
                                 double jerkOverride);
    int  profileOptions() const;
    const Axis* axis(const std::string& id) const;
    const Driver* driverOf(const Axis& a) const;
    // The profiles' rate over the controller's axes (all of them: none given): <linear, rotational, Euclidean>.
    struct Rates { double linear = 0, rotational = 0, euclidean = 0; };
    template <typename F> Rates rate(const std::string* driverId, F f) const;
    template <typename F> std::optional<double> rs274ngcRate(const std::string& driverId, F f, std::optional<double> fallback) const;
    double computeMaxDeltaA(int maxJerkSteps, size_t axisIndex) const;
    // OpenPnP's AxesLocation helpers, over the axes by id.
    Location segment(const Location& from, const Location& to, const std::string* driverId) const;
    bool isZero(const Location& l, double scale = 1) const;
    double rs274ngcMetric(const Location& l, const std::string& driverId) const;
    static double euclidean(const Location& l);

    std::vector<Axis>            m_axes;
    std::vector<Driver>          m_drivers;
    std::vector<JPMotionProfile> m_profiles;   // one per axis, in m_axes' order
    Location m_location0, m_location1;
    double   m_nominalSpeed = 1, m_effectiveSpeed = 1, m_euclideanDistance = 0;
    int      m_options = 0;
};

} // inline namespace jf
