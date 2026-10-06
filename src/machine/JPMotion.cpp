// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// A port of OpenPnP's Motion (Copyright (C) 2020 <mark@makr.zone>, GPL-3.0-or-later), kept to its structure so
// the two can be read side by side; OpenPnP's comments are kept where they explain why.

#include "JPMotion.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <set>

inline namespace jf {

namespace {

constexpr double kInf = std::numeric_limits<double>::infinity();
constexpr int kOrders = 3;   // velocity, acceleration, jerk

double signum(double x) { return x > 0 ? 1.0 : x < 0 ? -1.0 : x; }

double value(const JPMotion::Location& l, const std::string& id) {
    const auto it = l.find(id);
    return it == l.end() ? 0 : it->second;
}

} // namespace

bool JPMotion::Axis::matches(double a, double b) const {
    const double r = resolution > 0 ? resolution : kDefaultResolution;
    return std::llround(a / r) == std::llround(b / r);
}

JPMotion::JPMotion(std::vector<Axis> axes, const std::vector<Driver>& drivers, Location location0, Location location1,
                   double nominalSpeed, int options, double feedrateOverride, double accelerationOverride, double jerkOverride)
    : m_axes(std::move(axes)), m_drivers(drivers), m_location0(std::move(location0)), m_location1(std::move(location1)),
      m_nominalSpeed(nominalSpeed), m_options(options) {
    m_profiles.resize(m_axes.size());
    computeLimitsAndProfile(drivers, feedrateOverride, accelerationOverride, jerkOverride);
}

const JPMotion::Axis* JPMotion::axis(const std::string& id) const {
    for (const Axis& a : m_axes)
        if (a.id == id) return &a;
    return nullptr;
}

const JPMotion::Driver* JPMotion::driverOf(const Axis& a) const {
    for (const Driver& d : m_drivers)
        if (d.id == a.driverId) return &d;
    return nullptr;
}

int JPMotion::profileOptions() const {
    int o = 0;
    if (!hasOption(UncoordinatedMotion)) o |= JPMotionProfile::flag(JPMotionProfile::Coordinated);
    if (hasOption(SynchronizeEarlyBird)) o |= JPMotionProfile::flag(JPMotionProfile::SynchronizeEarlyBird);
    if (hasOption(SynchronizeLastMinute)) o |= JPMotionProfile::flag(JPMotionProfile::SynchronizeLastMinute);
    if (hasOption(SynchronizeStraighten)) o |= JPMotionProfile::flag(JPMotionProfile::SynchronizeStraighten);
    return o;
}

void JPMotion::computeLimitsAndProfile(const std::vector<Driver>&, double feedrateOverride, double accelerationOverride,
                                       double jerkOverride) {
    // The distance: only the axes in location1 that do not match location0's coordinates.
    Location distance;
    for (const Axis& a : m_axes) {
        const auto t = m_location1.find(a.id);
        if (t == m_location1.end()) continue;
        const double from = value(m_location0, a.id);
        if (!a.matches(from, t->second)) distance[a.id] = t->second - from;
    }
    // location1 with every axis: location0's, the moving ones moved.
    Location location1 = m_location0;
    for (const auto& [id, d] : distance) location1[id] = m_location1.at(id);
    for (const Axis& a : m_axes)
        if (!location1.count(a.id)) location1[a.id] = value(m_location1, a.id);
    m_location1 = location1;

    auto limits = [this](const Axis& a, double& sMin, double& sMax) {
        sMin = a.softLow.value_or(-kInf);
        sMax = a.softHigh.value_or(kInf);
        if (hasOption(LimitToSafeZone)) {
            if (a.safeLow) sMin = *a.safeLow;
            if (a.safeHigh) sMax = *a.safeHigh;
        }
    };

    if (distance.empty() || hasOption(UncoordinatedMotion)) {
        // Zero distance or uncoordinated motion: the axes' constraints apply directly.
        m_effectiveSpeed = m_nominalSpeed;
        bool linearMove = false;
        for (const auto& [id, d] : distance)
            if (std::abs(d) > 0 && !axis(id)->rotational) linearMove = true;
        for (size_t i = 0; i < m_axes.size(); i++) {
            const Axis& a = m_axes[i];
            double sMin, sMax;
            limits(a, sMin, sMax);
            const double d = std::abs(value(distance, a.id));
            double effectiveSpeedDerivatives = m_effectiveSpeed;
            int options = profileOptions();
            if (const Driver* drv = driverOf(a)) {
                if (a.limit[3] != 0 && drv->type == JPMotionControlType::SimpleSCurve)
                    options |= JPMotionProfile::flag(JPMotionProfile::SimplifiedSCurve);
                if (!drv->type.isSupportingUncoordinated()) options |= JPMotionProfile::flag(JPMotionProfile::RestrictToCoordinated);
                if (drv->type == JPMotionControlType::ToolpathFeedRate) effectiveSpeedDerivatives = 1.0;
            }
            const bool relevant = d > 0 && (a.rotational ^ linearMove);
            double vMax = m_effectiveSpeed * a.limit[1];
            if (relevant && feedrateOverride != 0) vMax = std::min(vMax, feedrateOverride);
            // The speed factor to the power of the derivative's order.
            double aMax = std::pow(effectiveSpeedDerivatives, 2) * a.limit[2];
            if (relevant && accelerationOverride != 0) aMax = std::min(aMax, accelerationOverride);
            double jMax = std::pow(effectiveSpeedDerivatives, 3) * a.limit[3];
            if (relevant && jerkOverride != 0) jMax = std::min(jMax, jerkOverride);
            // s0 by distance rather than location0, as some axes may have been left out of it.
            const double s1 = m_location1.at(a.id);
            const double s0 = s1 - value(distance, a.id);
            m_profiles[i] = JPMotionProfile(s0, s1, 0, 0, 0, 0, sMin, sMax, vMax, aMax, aMax, jMax, 0, kInf, options);
        }
        // Uncoordinated: synchronized, i.e. all take the same time.
        JPMotionProfile::synchronizeProfiles(m_profiles);
        m_euclideanDistance = euclidean(distance);
        return;
    }

    // Coordinated non-zero motion: the Euclidean distance of the linear, rotational and all axes, the most
    // limiting axis feed rate/acceleration/jerk per distance, and the most limiting controller feed rate.
    double linearLimits[kOrders + 1], rotationalLimits[kOrders + 1], overallLimits[kOrders + 1];
    linearLimits[0] = rotationalLimits[0] = overallLimits[0] = 0;
    for (int order = 1; order <= kOrders; order++) linearLimits[order] = rotationalLimits[order] = overallLimits[order] = kInf;
    double minDriverFeedrate = kInf;
    bool simpleSCurve = false;
    for (const auto& [id, dist] : distance) {
        const Axis& a = *axis(id);
        const double d = std::abs(dist), dSq = d * d;
        if (dSq <= 0) continue;
        const Driver* drv = driverOf(a);
        if (drv && !hasOption(NoDriverLimit) && drv->feedRatePerSecond != 0.0)
            minDriverFeedrate = std::min(minDriverFeedrate, drv->feedRatePerSecond);
        // In coordination the whole motion is a simplified S-curve as soon as one controller wants that.
        if (a.limit[3] != 0 && drv && drv->type == JPMotionControlType::SimpleSCurve) simpleSCurve = true;
        (a.rotational ? rotationalLimits[0] : linearLimits[0]) += dSq;
        overallLimits[0] += dSq;
        for (int order = 1; order <= kOrders; order++) {
            const double limit = a.limit[order];
            if (limit <= 0) continue;
            // The limits apply to the overall motion, but an axis only contributes a fraction of it: per unit of
            // its own length (divided by d here), normed to the overall distance after the loop.
            (a.rotational ? rotationalLimits[order] : linearLimits[order]) =
                std::min(a.rotational ? rotationalLimits[order] : linearLimits[order], limit / d);
            overallLimits[order] = std::min(overallLimits[order], limit / d);
        }
    }
    // The Euclidean metric from the sums of squares, and the fractional limits normed to it.
    linearLimits[0] = std::sqrt(linearLimits[0]);
    rotationalLimits[0] = std::sqrt(rotationalLimits[0]);
    overallLimits[0] = std::sqrt(overallLimits[0]);
    for (int order = 1; order <= kOrders; order++) {
        if (linearLimits[0] > 0) linearLimits[order] *= linearLimits[0];
        if (rotationalLimits[0] > 0) rotationalLimits[order] *= rotationalLimits[0];
        if (overallLimits[0] > 0) overallLimits[order] *= overallLimits[0];
    }
    // NIST RS274NGC 2.1.2.5: F over the linear axes' Euclidean distance, else over the rotational ones'. The factor
    // between the overall vector and the feed-rate relevant one.
    const double overallFactor = linearLimits[0] > 0 ? overallLimits[0] / linearLimits[0] : overallLimits[0] / rotationalLimits[0];
    if (feedrateOverride != 0) {
        if (linearLimits[0] > 0) linearLimits[1] = std::min(linearLimits[1], feedrateOverride);
        else rotationalLimits[1] = std::min(rotationalLimits[1], feedrateOverride);
        overallLimits[1] = std::min(overallLimits[1], feedrateOverride * overallFactor);
    } else if (!hasOption(NoDriverLimit)) {
        // The controller's feed rate caps the linear axes (a mm/s rate means nothing to a rotation: OpenPnP departs
        // from its former behaviour there).
        if (linearLimits[0] > 0) linearLimits[1] = std::min(linearLimits[1], minDriverFeedrate);
    }
    if (accelerationOverride != 0) {
        if (linearLimits[0] > 0) linearLimits[2] = std::min(linearLimits[2], accelerationOverride);
        else rotationalLimits[2] = std::min(rotationalLimits[2], accelerationOverride);
        overallLimits[2] = std::min(overallLimits[2], accelerationOverride * overallFactor);
    }
    if (jerkOverride != 0) {
        if (linearLimits[0] > 0) linearLimits[3] = std::min(linearLimits[3], jerkOverride);
        else rotationalLimits[3] = std::min(rotationalLimits[3], jerkOverride);
        overallLimits[3] = std::min(overallLimits[3], jerkOverride * overallFactor);
    }
    const double time = std::max(linearLimits[0] / linearLimits[1], rotationalLimits[0] / rotationalLimits[1]);
    const double euclideanTime = overallLimits[0] / overallLimits[1];
    // From the (perhaps controller-limited) RS274NGC feed rate to the Euclidean limit by the motion time, with the
    // speed factor.
    m_effectiveSpeed = (time > 0 ? euclideanTime / time : 1.0) * std::max(0.01, m_nominalSpeed);
    m_euclideanDistance = overallLimits[0];

    for (size_t i = 0; i < m_axes.size(); i++) {
        const Axis& a = m_axes[i];
        double effectiveSpeedDerivatives = m_nominalSpeed;
        const double d = value(distance, a.id);
        const double axisFraction = std::abs(d) / m_euclideanDistance;
        double sMin, sMax;
        limits(a, sMin, sMax);
        int options = profileOptions();
        if (const Driver* drv = d != 0.0 ? driverOf(a) : nullptr) {
            // In the motion with its controller set: the controller's restrictions.
            if (simpleSCurve) options |= JPMotionProfile::flag(JPMotionProfile::SimplifiedSCurve);
            if (!drv->type.isSupportingUncoordinated()) options |= JPMotionProfile::flag(JPMotionProfile::RestrictToCoordinated);
            if (drv->type == JPMotionControlType::ToolpathFeedRate) effectiveSpeedDerivatives = 1.0;
        }
        const double vMax = m_effectiveSpeed * overallLimits[1] * axisFraction;
        const double aMax = std::pow(effectiveSpeedDerivatives, 2) * overallLimits[2] * axisFraction;
        const double jMax = std::pow(effectiveSpeedDerivatives, 3) * overallLimits[3] * axisFraction;
        const double s1 = m_location1.at(a.id);
        const double s0 = s1 - d;
        m_profiles[i] = JPMotionProfile(s0, s1, 0, 0, 0, 0, sMin, sMax, vMax, aMax, aMax, jMax, 0, kInf, options);
    }
    JPMotionProfile::coordinateProfiles(m_profiles);
}

JPMotion::Location JPMotion::momentaryLocation(double t) const {
    Location l;
    for (size_t i = 0; i < m_axes.size(); i++) l[m_axes[i].id] = m_profiles[i].momentaryLocation(t);
    return l;
}

JPMotion::Location JPMotion::momentaryVelocity(double t) const {
    Location l;
    for (size_t i = 0; i < m_axes.size(); i++) l[m_axes[i].id] = m_profiles[i].momentaryVelocity(t);
    return l;
}

JPMotion::Location JPMotion::momentaryAcceleration(double t) const {
    Location l;
    for (size_t i = 0; i < m_axes.size(); i++) l[m_axes[i].id] = m_profiles[i].momentaryAcceleration(t);
    return l;
}

JPMotion::Location JPMotion::momentaryJerk(double t) const {
    Location l;
    for (size_t i = 0; i < m_axes.size(); i++) l[m_axes[i].id] = m_profiles[i].momentaryJerk(t);
    return l;
}

JPMotion::Location JPMotion::movingAxesTarget(const std::string& driverId) const {
    Location l;
    for (const auto& [id, d] : segment(m_location0, m_location1, &driverId)) l[id] = m_location1.at(id);
    return l;
}

JPMotion::Location JPMotion::segment(const Location& from, const Location& to, const std::string* driverId) const {
    // OpenPnP's motionSegmentTo (and drivenBy): the axes whose coordinates differ in their resolution ticks.
    Location seg;
    for (const Axis& a : m_axes) {
        if (driverId && a.driverId != *driverId) continue;
        const auto f = from.find(a.id), t = to.find(a.id);
        if (f == from.end() || t == to.end()) continue;
        if (!a.matches(f->second, t->second)) seg[a.id] = t->second - f->second;
    }
    return seg;
}

bool JPMotion::isZero(const Location& l, double scale) const {
    // OpenPnP's matches(AxesLocation.zero): each coordinate 0 in its axis's resolution ticks.
    for (const auto& [id, x] : l) {
        const Axis* a = axis(id);
        if (a && !a->matches(x * scale, 0)) return false;
    }
    return true;
}

double JPMotion::euclidean(const Location& l) {
    double sum = 0;
    for (const auto& [id, x] : l) sum += x * x;
    return std::sqrt(sum);
}

double JPMotion::rs274ngcMetric(const Location& l, const std::string& driverId) const {
    double linear = 0, rotational = 0;
    for (const auto& [id, x] : l) {
        const Axis* a = axis(id);
        if (!a || a->driverId != driverId) continue;
        (a->rotational ? rotational : linear) += x * x;
    }
    return linear != 0 ? std::sqrt(linear) : std::sqrt(rotational);
}

template <typename F>
JPMotion::Rates JPMotion::rate(const std::string* driverId, F f) const {
    Rates r;
    for (size_t i = 0; i < m_axes.size(); i++) {
        const Axis& a = m_axes[i];
        if (driverId && a.driverId != *driverId) continue;
        const double v = f(m_profiles[i]);
        (a.rotational ? r.rotational : r.linear) += v * v;
        r.euclidean += v * v;
    }
    r.linear = std::sqrt(r.linear);
    r.rotational = std::sqrt(r.rotational);
    r.euclidean = std::sqrt(r.euclidean);
    return r;
}

template <typename F>
std::optional<double> JPMotion::rs274ngcRate(const std::string& driverId, F f, std::optional<double> fallback) const {
    const Rates r = rate(&driverId, f);
    if (r.linear != 0.0) return r.linear;
    if (r.rotational != 0.0) return r.rotational;
    return fallback;
}

std::optional<double> JPMotion::feedRatePerSecond(const Driver& d) const {
    std::optional<double> fallback = d.feedRatePerSecond * m_nominalSpeed;
    if (*fallback == 0.0) fallback.reset();
    if (d.type == JPMotionControlType::ToolpathFeedRate) return fallback;
    return rs274ngcRate(d.id, [&d](const JPMotionProfile& p) { return p.profileVelocity(d.type); }, fallback);
}

std::optional<double> JPMotion::accelerationPerSecond2(const Driver& d) const {
    if (d.type == JPMotionControlType::ToolpathFeedRate) return std::nullopt;
    return rs274ngcRate(d.id, [&d](const JPMotionProfile& p) { return p.profileAcceleration(d.type); }, std::nullopt);
}

std::optional<double> JPMotion::jerkPerSecond3(const Driver& d) const {
    if (d.type == JPMotionControlType::ToolpathFeedRate) return std::nullopt;
    return rs274ngcRate(d.id, [&d](const JPMotionProfile& p) { return p.profileJerk(d.type); }, std::nullopt);
}

double JPMotion::computeMaxDeltaA(int maxJerkSteps, size_t i) const {
    const JPMotionProfile& p = m_profiles[i];
    if (p.isConstantAcceleration() || maxJerkSteps < 2) return kInf;
    const double profileAcceleration = std::max(std::abs(p.higherABoundary()), std::abs(p.lowerABoundary()));
    const double deltaA = p.accelerationMax() / maxJerkSteps;
    const double steps = std::max(1.0, double(std::llround(profileAcceleration / deltaA)));
    return profileAcceleration / steps * 0.999;
}

std::vector<JPMotion::MoveTo> JPMotion::interpolatedMoveToCommands(const Driver& driver, bool retiming) {
    if (driver.type == JPMotionControlType::ModeratedConstantAcceleration) return moderatedMoveTo(driver);
    if (!driver.type.isInterpolated()) return singleMoveTo(driver);

    // A simple constant acceleration move.
    if (JPMotionProfile::isCoordinated(m_profiles)) {
        bool hasJerk = false;
        for (const JPMotionProfile& p : m_profiles)
            if (p.profileVelocity(JPMotionControlType::ConstantAcceleration) != 0 && !p.isConstantAcceleration()) {
                hasJerk = true;
                break;
            }
        if (!hasJerk) return moderatedMoveTo(driver);
    }
    const double time = this->time();
    const int maxSteps = driver.interpolationMaxSteps, maxJerkSteps = driver.interpolationJerkSteps;
    double timeStep = driver.interpolationTimeStep;
    int distStep = driver.interpolationMinStep;
    const double minVelocity = driver.minimumRate[1], minAcceleration = driver.minimumRate[2];

    // The speed factor applied to the interpolation, for the same number of steps.
    timeStep /= m_nominalSpeed;
    int numSteps = int(std::floor(time / timeStep / 2)) * 2;
    if (numSteps < 4) return moderatedMoveTo(driver);   // too short to interpolate
    distStep = std::max(3, distStep);

    // Each axis's largest change in acceleration, simulating jerk control.
    Location maxDeltaA;
    for (size_t i = 0; i < m_axes.size(); i++)
        if (m_axes[i].driverId == driver.id) maxDeltaA[m_axes[i].id] = std::max(minAcceleration * 5, computeMaxDeltaA(maxJerkSteps, i));

    const Location jerk0 = momentaryJerk(0), jerkEnd = momentaryJerk(time - JPMotionProfile::kTtol);
    bool jerkMatches = true;
    for (const auto& [id, j] : jerk0)
        if (!axis(id)->matches(j, value(jerkEnd, id))) jerkMatches = false;
    const bool simpleSymmetricMove = isZero(momentaryVelocity(0)) && isZero(momentaryVelocity(time)) && isZero(momentaryAcceleration(time))
                                     && jerkMatches;
    if (simpleSymmetricMove) {
        double wantedTimeStep = kInf;
        for (const auto& [id, da] : maxDeltaA) {
            const double j = std::abs(value(jerk0, id));
            if (std::isfinite(da) && j > 0) wantedTimeStep = std::min(wantedTimeStep, da / j);
        }
        const int numStepsNew = int(std::ceil(2 * time / wantedTimeStep));
        if (numStepsNew < 4) return moderatedMoveTo(driver);
        numSteps = std::min(numSteps, numStepsNew);
    }

    // Junction deviation (per axis) as the allowed instant change of velocity: s = 1/2 dV^2/a, so
    // dV = sqrt(2)*sqrt(a*s); half of it, as entry and exit are treated apart.
    Location maxDeltaV;
    for (size_t i = 0; i < m_axes.size(); i++)
        if (m_axes[i].driverId == driver.id)
            maxDeltaV[m_axes[i].id] = 1. / 2 * std::sqrt(2) * std::sqrt(driver.junctionDeviation * m_profiles[i].accelerationMax());

    // The interpolation steps through the move in small time steps, each time testing a straight segment from the
    // last interpolation point to the step's. That is refused where (1) the distance is too small (the axes'
    // resolution would degrade the vectors), (2) the instant change of velocity at a corner of the polygon an
    // uncoordinated curve becomes is too large, (3) the instant change of acceleration, simulating jerk control
    // in steps, is too large, or (4) the acceleration has already reached or left a plateau. For (1) the next time
    // step is taken (the last merged with the one before); for the others a new segment is made at the previous
    // valid time step, or at this one when there is none (only in very tight curves, at low speed).

    // The special times: each profile's segment times, the location extremes, and velocity peaks.
    std::set<double> intervals { 0. }, intervalsExtremes, motionIntervals;
    for (const JPMotionProfile& p : m_profiles) {
        double t = p.segmentTime(0);
        for (int i = 1; i <= JPMotionProfile::kSegments + 1; i++) {
            intervals.insert(t);
            t += p.segmentTime(i);
        }
        // The location extremes, where the velocity inverts, and the velocity peaks (when it does not cruise).
        intervalsExtremes.insert(p.lowerSBoundaryTime());
        intervalsExtremes.insert(p.higherSBoundaryTime());
        if (p.segmentTime(4) < JPMotionProfile::kTtol) {
            intervalsExtremes.insert(p.lowerVBoundaryTime());
            intervalsExtremes.insert(p.higherVBoundaryTime());
        }
    }
    // Filter the intervals into the motion's special times.
    double tPrev = -1, tConstantA = std::numeric_limits<double>::quiet_NaN();
    int constantV = 0, constantA = 0;
    for (double t : intervals) {
        if (t <= tPrev + JPMotionProfile::kEps) continue;
        const Location velocity = momentaryVelocity(t);
        const Location acceleration = momentaryAcceleration(t + JPMotionProfile::kEps);
        const Location jerk = momentaryJerk(t + JPMotionProfile::kEps);
        if (t > 0 && intervalsExtremes.count(t)) motionIntervals.insert(t);   // a location extreme
        if (t > 0 && isZero(acceleration) && isZero(jerk)) {
            if (!isZero(velocity)) {
                if (constantV == 0) motionIntervals.insert(t);   // constant V begins
                constantV++;
            }
        } else {
            if (constantV > 0) motionIntervals.insert(t);   // the constant V plateau ends
            constantV = 0;
        }
        if (!isZero(acceleration) && isZero(jerk)) {
            if (constantA == 0) tConstantA = t;   // constant a begins
            constantA++;
        } else if (constantA > 0) {
            if (t - tConstantA > timeStep * 8) {
                if (tConstantA > 0) motionIntervals.insert(tConstantA);
                motionIntervals.insert(t);
            }
            constantA = 0;
        }
        tPrev = t;
    }

    std::vector<MoveTo> list;
    list.reserve(size_t(numSteps));
    // The last interpolation point taken, the start to begin with.
    Location location0 = momentaryLocation(0), velocity0 = momentaryVelocity(0), acceleration0 = momentaryAcceleration(0);
    double t0 = 0;
    std::optional<MoveTo> command0;
    // The last candidate interpolation point.
    Location location1 = location0, velocity1 = velocity0, acceleration1 = acceleration0;
    double t1 = 0;
    std::optional<MoveTo> command1;
    // The second-last interpolation point taken.
    Location locationS = location0, velocityS = velocity0, accelerationS = acceleration0;
    double tS = 0;
    std::optional<MoveTo> commandS;

    double maxVelocity = minVelocity;
    const double dt = time / numSteps;
    bool interpolationNeeded = false;
    const bool coordinated = JPMotionProfile::isCoordinated(m_profiles);
    for (int i = 1; i <= numSteps; i++) {
        double t2 = i * dt;
        bool special = (i == numSteps);
        // Snapped to any special time.
        while (!motionIntervals.empty() && *motionIntervals.begin() <= t2 + dt * .5) {
            t2 = *motionIntervals.begin();
            motionIntervals.erase(motionIntervals.begin());
            special = true;
        }
        const Location location2 = momentaryLocation(t2);
        const Location acceleration2 = momentaryAcceleration(t2);
        if (!special && isZero(acceleration2) && isZero(acceleration1)) continue;   // a straight line, nothing happens
        // When the candidate segment is added, the analysis is repeated with the new origin.
        while (true) {
            Location seg = segment(location0, location2, &driver.id);
            bool isTooSmall = seg.empty() || isZero(seg, 1.0 / distStep);
            if (special && isTooSmall) {
                // The last step's distance under distStep resolution ticks: merged with the segment before.
                if (command1) {
                    command1.reset();
                } else {
                    // No candidate: merged with the previous command.
                    if (!list.empty()) list.pop_back();
                    location0 = locationS;
                    velocity0 = velocityS;
                    acceleration0 = accelerationS;
                    t0 = tS;
                    command0 = commandS;
                    seg = segment(location0, location2, &driver.id);
                    isTooSmall = seg.empty() || isZero(seg, 1.0 / distStep);   // it may still be too small
                }
            }
            if (isTooSmall) break;
            const double distance = rs274ngcMetric(seg, driver.id);
            Location moved;
            for (const auto& [id, d] : seg) moved[id] = location2.at(id);
            const Location velocity2 = momentaryVelocity(t2);
            // Curved, the segments have angles between them (a polygon's corners): the velocity projected onto the
            // straight segment, a little lower, with an instant change of velocity at the corner (a controller's
            // "junction deviation" or "jerk").
            auto along = [&seg](const Location& v) {
                double dot = 0, norm2 = 0;
                for (const auto& [id, d] : seg) {
                    dot += value(v, id) * d;
                    norm2 += d * d;
                }
                Location out;
                for (const auto& [id, d] : seg) out[id] = norm2 == 0 ? 0 : d * dot / norm2;
                return out;
            };
            const Location segmentVelocity0 = along(velocity0), segmentVelocity2 = along(velocity2);
            // The scalar RS274NGC (G-code) tool-path rates.
            const double v0 = rs274ngcMetric(segmentVelocity0, driver.id), v2 = rs274ngcMetric(segmentVelocity2, driver.id);
            // The average velocity with constant acceleration; the tool-path acceleration over the nominal time.
            const double avgVelocity = (v0 + v2) * 0.5;
            const double dtNominal = distance == 0 ? 0 : distance / avgVelocity;
            const double acceleration = (v2 - v0) / dtNominal;
            // The peak velocity, recorded even when this segment is not taken (the true peak is wanted).
            const double maxSegmentVelocity = std::max(std::abs(v0), std::abs(v2));
            maxVelocity = std::max(maxSegmentVelocity, maxVelocity);
            double minSegmentAcceleration = minAcceleration;
            std::optional<double> velocity;
            if (acceleration == 0) {
                // Governed by velocity; a higher acceleration allowed, to recover from any unplanned deceleration.
                velocity = maxSegmentVelocity;
                minSegmentAcceleration = kInf;
                for (const auto& [id, d] : seg) minSegmentAcceleration = std::min(minSegmentAcceleration, value(maxDeltaA, id) / std::abs(d / distance));
            }
            std::optional<MoveTo> command2 = MoveTo { location0, location2, moved, velocity,
                                                      std::max(std::max(std::abs(acceleration), minSegmentAcceleration), minAcceleration),
                                                      std::nullopt,   // no jerk: it is simulated
                                                      t0, dtNominal, v0, v2 };
            // A new segment?
            bool newSegment = false;
            if (special) {
                newSegment = true;
                command1.reset();
            } else {
                auto deltaVTooLarge = [&](const Location& v, const Location& segV) {
                    for (const auto& [id, x] : v)
                        if (std::abs(x - value(segV, id)) > value(maxDeltaV, id)) return true;
                    return false;
                };
                // The instant change of velocity on entry, then on exit.
                if (!coordinated && (deltaVTooLarge(velocity0, segmentVelocity0) || deltaVTooLarge(velocity2, segmentVelocity2))) {
                    newSegment = true;
                    interpolationNeeded = true;
                }
                if (!newSegment) {
                    // The acceleration: jerk control simulated.
                    for (const auto& [id, d] : seg) {
                        const double da20 = std::abs(value(acceleration2, id) - value(acceleration0, id));
                        if (da20 * 1.02 > value(maxDeltaA, id)) {
                            command1.reset();
                            newSegment = true;
                            interpolationNeeded = true;
                            break;
                        }
                    }
                }
            }
            if (!newSegment) {
                // The new candidate; on to the next time step.
                t1 = t2;
                location1 = location2;
                velocity1 = velocity2;
                acceleration1 = acceleration2;
                command1 = command2;
                break;
            }
            if (int(list.size()) >= maxSteps - 1) {
                // Not enough steps to interpolate: degraded to a moderated move.
                setOption(InterpolationFailed);
                return moderatedMoveTo(driver);
            }
            if (!command1) {
                // No candidate before this one: taken fully.
                t1 = t2;
                location1 = location2;
                velocity1 = velocity2;
                acceleration1 = acceleration2;
                command1 = command2;
                command2.reset();
            }
            list.push_back(*command1);
            // The previous segment's beginning remembered.
            tS = t0;
            locationS = location0;
            velocityS = velocity0;
            accelerationS = acceleration0;
            commandS = command0;
            // Shifted one segment.
            t0 = t1;
            location0 = location1;
            velocity0 = velocity1;
            acceleration0 = acceleration1;
            command0 = command1;
            command1.reset();
            if (!command2) break;   // no previous candidate: out of the inner loop
            // Else on in the inner loop: the shifted segment made.
        }
    }
    // The last candidate, if left over.
    if (command1) list.push_back(*command1);
    if (list.size() < 2 || !interpolationNeeded) return moderatedMoveTo(driver);   // the interpolation collapsed

    // Constant acceleration between the waypoints is a little faster: retimed to the planned time exactly.
    double timeEffective = 0;
    for (const MoveTo& m : list) timeEffective += *m.time;
    const double factor = retiming ? timeEffective / time : 1.0, factorSq = factor * factor;
    // The maximum for the whole move.
    list[0].feedRatePerSecond = maxVelocity;
    double tSum = 0;
    for (MoveTo& m : list) {
        if (m.feedRatePerSecond) *m.feedRatePerSecond *= factor;
        if (m.v0) *m.v0 *= factor;
        if (m.v1) *m.v1 *= factor;
        if (m.accelerationPerSecond2) *m.accelerationPerSecond2 *= factorSq;
        *m.time /= factor;
        m.t0 = tSum;
        tSum += *m.time;
    }
    return list;
}

std::vector<JPMotion::MoveTo> JPMotion::moderatedMoveTo(const Driver& driver) const {
    // A constant acceleration move taking the same time as the 3rd order one: similar average acceleration and peak
    // feed rate, more defensive short moves and quicker long ones (and a fair comparison of the two).
    const std::vector<double> unitVector = JPMotionProfile::unitVector(m_profiles);
    const int leadAxis = JPMotionProfile::leadAxisIndex(unitVector);
    const JPMotionProfile& profile = m_profiles[size_t(leadAxis)];
    const double time = this->time();
    // The minimum acceleration move.
    const double s = profile.location(7) - profile.location(0);
    const double vmax = profile.velocityMax();
    const double t = time;
    double v0 = profile.velocity(0), v7 = profile.velocity(7);
    // No reversal of velocity: as from still-stand (failed interpolation can give it).
    const double sgn = signum(s);
    if (sgn != signum(v0)) v0 = 0;
    if (sgn != signum(v7)) v7 = 0;
    // Sage: acceleration only.
    double v = 1. / 2 * (2 * s + sgn * std::sqrt(2 * t * t * v0 * v0 + 2 * t * t * v7 * v7 - 4 * s * t * v0 - 4 * s * t * v7 + 4 * s * s)) / t;
    double a;
    if (std::abs(v) <= vmax) {
        a = (2 * v - v0 - v7) / t;   // Vmax not reached
    } else {
        // Sage: with Vmax.
        v = sgn * vmax;
        a = 1. / 2 * (2 * v * v - 2 * v * v0 + v0 * v0 - 2 * v * v7 + v7 * v7) / (t * v - s);
    }
    // From the lead axis to the rate along the relevant axes (linear, else rotational: RS274NGC).
    const Location seg = segment(m_location0, m_location1, nullptr);
    const double distance = rs274ngcMetric(seg, driver.id);
    const double factor = distance / euclidean(seg) / std::abs(unitVector[size_t(leadAxis)]);
    return { MoveTo { m_location0, m_location1, movingAxesTarget(driver.id), std::max(driver.minimumRate[1], std::abs(factor * v)),
                      std::max(driver.minimumRate[2], std::abs(factor * a)), std::nullopt, 0.0, time, std::abs(factor * v0),
                      std::abs(factor * v7) } };
}

std::vector<JPMotion::MoveTo> JPMotion::singleMoveTo(const Driver& driver) const {
    // The profile's rates (or limits), regardless of how the controller executes it.
    return { MoveTo { m_location0, m_location1, movingAxesTarget(driver.id), feedRatePerSecond(driver), accelerationPerSecond2(driver),
                      jerkPerSecond3(driver), 0.0, std::nullopt, std::nullopt, std::nullopt } };
}

} // inline namespace jf
