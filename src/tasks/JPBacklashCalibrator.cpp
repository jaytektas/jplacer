// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPBacklashCalibrator.h"

#include "JPCameraLook.h"

#include "common/JPlacerLog.h"
#include "JPPipelineMarkFinder.h"

#include <j/core/Log.h>

#include <algorithm>
#include <cmath>
#include <optional>
#include <chrono>
#include <limits>
#include <cstdio>
#include <ctime>
#include <random>

inline namespace jf {

namespace {

// OpenPnP's CalibrationSolutions constants: the test moves, the step test, the distances' factor, the passes
// finding the offset at a speed and their dampening, the finest tolerance, and vision's super-sampling.
constexpr double kTestMoveMm = 10;
constexpr double kLargeMoveMm = 90;
constexpr double kStepTestMm = 1;
const double     kDistanceFactor = std::sqrt(2.0);
constexpr int    kPasses = 4;
constexpr double kDampening = 0.9;
constexpr double kFinestMm = 0.01;
constexpr double kSuperSampling = 4;
// How far from where it is expected the mark is looked for (mm): the play
// and the camera's offset error together are far less.
constexpr double kSearchMm = 1.0;

std::string now() {
    const std::time_t t = std::time(nullptr);
    char buf[32];
    std::strftime(buf, sizeof buf, "%Y-%m-%d %H:%M:%S", std::localtime(&t));
    return buf;
}

// The lag after travelling each distance since turning: half the play
// measured coming in that far (the play is the lag one way and the other),
// made never to fall as the distance grows (pooling neighbours that do: the
// measuring's own wobble), as JPAxisConfig::backlashTable keeps it.
std::vector<std::pair<double, double>> lagTable(const std::vector<std::pair<double, double>>& byDistance) {
    struct Pool { double sum; int n; double first, last; };
    std::vector<Pool> pools;
    for (const auto& [d, play] : byDistance) {
        pools.push_back({ play / 2, 1, d, d });
        while (pools.size() > 1 && pools[pools.size() - 2].sum / pools[pools.size() - 2].n > pools.back().sum / pools.back().n) {
            Pool p = pools.back();
            pools.pop_back();
            pools.back().sum += p.sum;
            pools.back().n += p.n;
            pools.back().last = p.last;
        }
    }
    std::vector<std::pair<double, double>> out;
    for (const auto& [d, play] : byDistance)
        for (const Pool& p : pools)
            if (d >= p.first && d <= p.last) out.push_back({ d, p.sum / p.n });
    return out;
}

// Puts the compensation back as it was, whatever happens.
struct CompensationOff {
    JPCell& cell;
    bool    was;
    explicit CompensationOff(JPCell& c) : cell(c), was(c.backlashCompensation()) { cell.setBacklashCompensation(false, "backlash is being calibrated"); }
    ~CompensationOff() { cell.setBacklashCompensation(was, "Backlash Compensation unticked on the Jog panel"); }
};

} // namespace

JPBacklashCalibrator::Result JPBacklashCalibrator::run(JPCell& cell, JPCameraFeed& feed, const std::string& axisId,
                                                       const Options& o, const Progress& progress) {
    Result r;
    const JPMountConfig& mount = feed.config().mount;
    const bool alongX = mount.axisX == axisId;
    if (!alongX && mount.axisY != axisId) {
        r.why = feed.config().name + " does not ride on this axis: calibrate an X or Y axis the head camera moves on";
        return r;
    }
    const JPAxisConfig* axis = cell.config().axis(axisId);
    if (!axis) {
        r.why = "no such axis";
        return r;
    }
    if (!cell.isHomed()) {
        r.why = "home the machine first";
        return r;
    }
    JPCameraCalibration cal;
    if (!JPCameraLook::calibration(cell, feed, cal, r.why)) return r;
    const JPAxisConfig::Backlash wasMethod = axis->backlash;
    const double wasOffset = axis->backlashOffset, wasSneak = axis->sneakUpMm, wasSpeed = axis->backlashSpeedFactor;
    const auto wasTable = axis->backlashTable;
    const double wasApproach = axis->approachMm;
    // The way to compensate chosen.
    struct Choice {
        JPAxisConfig::Backlash method = JPAxisConfig::Backlash::None;
        double offset = 0, sneakUpMm = 0, speedFactor = 1;
    };
    Choice chosen;
    double maxBacklash = -std::numeric_limits<double>::infinity(), maxBacklashDistance = 0;
    double minBacklash = std::numeric_limits<double>::infinity();
    bool open = true;

    // The axis's place with the camera over the mark, and the other axis's.
    const double target = alongX ? o.markX - mount.offsetX : o.markY - mount.offsetY;
    const std::string otherId = alongX ? mount.axisY : mount.axisX;
    const double other = alongX ? o.markY - mount.offsetY : o.markX - mount.offsetX;
    auto go = [&](double at, double speed) {
        return cell.moveAxesAndWait({ { axisId, at }, { otherId, other } }, speed, r.why);
    };
    // Where the mark seems to be along the axis, the camera sent to `at`: the
    // camera's own error from where it was sent, the other way round.
    auto measure = [&](double at, double& along) {
        const double viewX = alongX ? at + mount.offsetX : o.markX, viewY = alongX ? o.markY : at + mount.offsetY;
        // The mark in several pictures, once settled, its place the mean: by the camera's calibration pipeline.
        double sumX = 0, sumY = 0;
        const int frames = std::max(1, o.frames);
        for (int f = 0; f < frames; ++f) {
            JPFrame img;
            if (!(f == 0 ? JPCameraLook::settled(feed, img, r.why) : JPCameraLook::takenFrame(feed, img, r.why, 1))) return false;
            double diameter = o.markDiameterMm, fx = 0, fy = 0;
            const JPRoundMark m = JPPipelineMarkFinder::onMachine(feed.config(), cal, img, o.markX, o.markY, viewX, viewY, kSearchMm,
                                                                  diameter, 0, 0, fx, fy);
            if (!m.found) {
                r.why = "the mark was not found: " + m.why;
                return false;
            }
            sumX += fx;
            sumY += fy;
        }
        const double mx = sumX / frames, my = sumY / frames;
        along = alongX ? mx : my;
        return true;
    };
    // OpenPnP's displacedAxisLocation: `d` along the axis from the mark, within its soft limits; full range: the
    // limit itself, that side.
    auto displaced = [&](double d, bool fullRange) {
        const double low = axis->softLimitLowEnabled ? axis->softLimitLow : -std::numeric_limits<double>::infinity();
        const double high = axis->softLimitHighEnabled ? axis->softLimitHigh : std::numeric_limits<double>::infinity();
        if (fullRange && std::isfinite(d < 0 ? low : high)) return d < 0 ? low : high;
        return std::max(low, std::min(high, target + d));
    };
    // OpenPnP's backlash calibration (CalibrationSolutions), its constants and steps.
    const double minimumSpeed = kSpeeds[0];
    // Its tolerance and step: the axis's resolution taken to at least 0.01 mm, else a quarter of a pixel.
    double resolution = axis->resolution > 0 ? axis->resolution * std::ceil(kFinestMm / axis->resolution) : kFinestMm;
    const double subPixel = cal.scale() > 0 ? 1.0 / cal.scale() / kSuperSampling : 0;
    if (resolution < subPixel) resolution = subPixel;
    const double tol = resolution, stepMm = resolution;
    r.data.toleranceMm = tol;
    // In to the mark from `sign` (+1 above, -1 below) by `distance`, first `wound` from the far side that way
    // (kLargeMoveMm, or the soft limit when `fullRange`), the last of it at the minimum speed: where it is found.
    auto from = [&](double sign, double distance, bool wound, bool fullRange, double& along) {
        if (wound && !go(displaced(sign * kLargeMoveMm, fullRange), o.speed)) return false;
        return go(displaced(sign * distance, fullRange), o.speed) && go(target, minimumSpeed) && measure(target, along);
    };

    {
        CompensationOff off(cell);
        // 1. Speed control: a slower speed factor must move slower (timed both ways over kTestMoveMm).
        if (progress) progress("checking speed control");
        if (!go(target, o.speed)) return r;
        auto timed = [&](double to, double speed, double& seconds) {
            const auto t0 = std::chrono::steady_clock::now();
            if (!go(to, speed)) return false;
            seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
            return true;
        };
        double out = 0, back = 0;
        if (!timed(displaced(-kTestMoveMm, false), 1.0, out) || !timed(target, 1.0, back)) return r;
        const double baseline = (out + back) / 2;
        for (double sp : kSpeeds) {
            double t = 0;
            if (!timed(displaced(-kTestMoveMm, false), sp, t) || !timed(target, sp, t)) return r;
            const double effective = t > 0 ? baseline / t : 1;
            if (sp == minimumSpeed && effective > std::sqrt(sp)) {
                char said[200];
                std::snprintf(said, sizeof said, "speed factor control seems not to be effective: should move at %d%%, but moved at "
                              "%d%%. Check the controller's motion control and the axis's configuration (it must use "
                              "acceleration control)", int(sp * 100), int(effective * 100));
                r.why = said;
                return r;
            }
        }
        // 2. A step test over a millimetre: each step, from a millimetre before it, at the minimum speed, must land
        //    where sent (else the camera's scale or the mark's height is wrong).
        if (progress) progress("step test");
        if (!go(displaced(-kLargeMoveMm, false), o.speed)) return r;
        std::optional<double> reference;
        double absoluteErr = 0;
        for (double stepPos = -kStepTestMm / 2; stepPos < kStepTestMm / 2; stepPos += stepMm) {
            double found = 0;
            if (!go(displaced(stepPos - kStepTestMm, false), o.speed) || !go(target + stepPos, minimumSpeed)
                || !measure(target + stepPos, found))
                return r;
            if (!reference) reference = found;
            else absoluteErr = found - *reference;
        }
        if (std::abs(absoluteErr) > 2 * tol) {
            char said[240];
            std::snprintf(said, sizeof said, "large absolute error: the steps moved %.4f mm too far over %.1f mm. Check the camera's "
                          "calibration (its scale) and the mark's height", absoluteErr, kStepTestMm);
            r.why = said;
            return r;
        }
        // 3. The backlash over distances (a geometric series, longest first, ending at the full range): coming in
        //    from each side, first from where it stands, then wound from the far side; the largest play and the
        //    shortest distance still showing it (to within twice the tolerance), up to kMaxSneakUpMm.
        std::vector<double> distances;
        double last = -std::numeric_limits<double>::infinity();
        for (double d = stepMm * 2; d <= kTestMoveMm; d *= kDistanceFactor)
            if (d - last >= stepMm) {
                distances.push_back(d);
                last = d;
            }
        distances.push_back(kLargeMoveMm);
        std::reverse(distances.begin(), distances.end());
        if (!go(target, o.speed)) return r;
        for (size_t i = 0; i < distances.size(); ++i) {
            const double d = distances[i];
            char said[120];
            std::snprintf(said, sizeof said, "the backlash at sneak-up distance %.3f mm (%zu of %zu)", d, i + 1, distances.size());
            if (progress) progress(said);
            for (int reverse = 1; reverse >= 0; --reverse) {
                if (reverse == 1 && d > kTestMoveMm) continue;
                const bool fullRange = d > kTestMoveMm;
                double below = 0, above = 0;
                if (!from(-1, d, reverse == 0, fullRange, below) || !from(+1, d, reverse == 0, fullRange, above)) return r;
                const double e = below - above;
                if (reverse == 0) r.data.byDistance.push_back({ d, e });
                if (d > maxBacklash && d <= kMaxSneakUpMm) {
                    if (open && e >= maxBacklash - 2 * tol) {
                        maxBacklash = std::max(maxBacklash, e);
                        maxBacklashDistance = d;
                        minBacklash = std::min(minBacklash, e);
                    } else {
                        open = false;
                    }
                }
            }
        }
        std::reverse(r.data.byDistance.begin(), r.data.byDistance.end());
    }
    if (std::isinf(maxBacklash)) {
        r.why = "no backlash measured within " + std::to_string(kMaxSneakUpMm) + " mm";
        return r;
    }
    const double sneakUp = std::max(maxBacklash, maxBacklashDistance);
    // 4. The offset at each speed: compensated (sneaking up the distance found, below full speed), in to the mark
    //    from kLargeMoveMm either side, the offset corrected by what is left (damped), up to kPasses times.
    for (double sp : kSpeeds) {
        char said[96];
        std::snprintf(said, sizeof said, "the backlash offset, speed factor %.0f%% (the sneak-up's speed)", sp * 100);
        if (progress) progress(said);
        cell.setBacklash(axisId, sp < 1 ? JPAxisConfig::Backlash::DirectionalSneakUp : JPAxisConfig::Backlash::Directional, 0,
                         sp < 1 ? sneakUp : 0, sp);
        double offset = 0;
        for (int pass = 0; pass < kPasses; ++pass) {
            double below = 0, above = 0;
            if (!go(displaced(-kLargeMoveMm, false), o.speed) || !go(target, 1.0) || !measure(target, below)
                || !go(displaced(kLargeMoveMm, false), o.speed) || !go(target, 1.0) || !measure(target, above)) {
                cell.setBacklash(axisId, wasMethod, wasOffset, wasSneak, wasSpeed, wasTable, wasApproach);
                return r;
            }
            const double e = below - above;
            offset += e * kDampening;
            if (pass == 0 && e <= -tol) break;   // overshoot: cannot compensate
            cell.setBacklash(axisId, sp < 1 ? JPAxisConfig::Backlash::DirectionalSneakUp : JPAxisConfig::Backlash::Directional,
                             offset, sp < 1 ? sneakUp : 0, sp);
            if (std::abs(e) < tol) break;
        }
        r.data.bySpeed.push_back({ sp, offset });
    }
    // 5. The method, by OpenPnP's rules.
    {
        int consistent = 0;
        double sum = 0;
        for (const auto& [sp, e] : r.data.bySpeed) {
            if (e <= -tol || std::abs(e - r.data.bySpeed.front().second) > tol) break;
            sum += e;
            ++consistent;
        }
        const double average = consistent ? sum / consistent : 0;
        constexpr size_t kAll = std::size(kSpeeds);
        if (sneakUp > o.mostSneakUpMm) {
            // No sneak-up short enough: one-sided, past the place by the largest play.
            chosen = { JPAxisConfig::Backlash::OneSided, maxBacklash, 0, kSpeeds[0] };
        } else if (average < sneakUp - tol) {
            // Even at the slowest, it takes coming in further than the play to show all of it.
            chosen = { JPAxisConfig::Backlash::DirectionalSneakUp, (minBacklash + maxBacklash) / 2, sneakUp, kSpeeds[0] };
        } else if (size_t(consistent) == kAll) {
            // The same at every speed: directional (none, under the tolerance).
            chosen = { average < tol ? JPAxisConfig::Backlash::None : JPAxisConfig::Backlash::Directional, average, 0,
                       kSpeeds[consistent - 1] };
        } else if (consistent > 0) {
            chosen = { JPAxisConfig::Backlash::DirectionalSneakUp, average, sneakUp, kSpeeds[consistent - 1] };
        } else {
            cell.setBacklash(axisId, wasMethod, wasOffset, wasSneak, wasSpeed, wasTable, wasApproach);
            r.why = axis->name + " seems to overshoot, even at the lowest speed factor: make sure the controller has "
                    "effective acceleration control. Automatic compensation is not possible.";
            return r;
        }
    }

    // 6. Tried, as OpenPnP tests its result: in from random distances either way (and from each side from afar),
    // each against the mean of them all: how well moves agree with each other, whatever the machine slowly drifts
    // by over the run.
    std::mt19937 rng(std::random_device{}());
    std::uniform_real_distribution<double> logDistance(std::log(std::max(2 * r.data.toleranceMm, 0.01)), std::log(kLargeMoveMm));
    std::bernoulli_distribution side(0.5);
    std::vector<double> distances = { -kLargeMoveMm, kLargeMoveMm };
    for (int i = 0; i < o.tries; ++i) distances.push_back(std::exp(logDistance(rng)) * (side(rng) ? 1 : -1));
    cell.setBacklash(axisId, chosen.method, chosen.offset, chosen.sneakUpMm, chosen.speedFactor);
    if (progress) progress(std::string("trying ") + JPAxisConfig::backlashWord(chosen.method));
    std::vector<std::pair<double, double>> tried;
    for (double d : distances) {
        double a;
        if (!go(displaced(d, false), o.speed) || !go(target, o.speed) || !measure(target, a)) {
            cell.setBacklash(axisId, wasMethod, wasOffset, wasSneak, wasSpeed, wasTable, wasApproach);
            return r;
        }
        tried.push_back({ d, a });
    }
    double mean = 0;
    for (const auto& [d, a] : tried) mean += a;
    mean /= double(tried.size());
    double worst = 0;
    for (const auto& [d, a] : tried) worst = std::max(worst, std::abs(a - mean));
    r.method = chosen.method;
    r.offset = chosen.offset;
    r.sneakUpMm = chosen.sneakUpMm;
    r.speedFactor = chosen.speedFactor;
    r.worstAfterMm = worst;
    for (const auto& [d, a] : tried) r.data.after.push_back({ d, a - mean });
    r.data.when = now();
    r.ok = true;
    JLOGC(JPlacerLog::kCell, JLogLevel::Info) << axis->name << " backlash: " << JPAxisConfig::backlashWord(r.method)
        << " offset " << r.offset << " sneak-up " << r.sneakUpMm << " speed " << r.speedFactor
        << ", tolerance " << r.data.toleranceMm << ", worst after " << r.worstAfterMm;
    return r;
}

} // inline namespace jf
