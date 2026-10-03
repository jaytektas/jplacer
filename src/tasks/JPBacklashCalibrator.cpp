// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPBacklashCalibrator.h"

#include "JPCameraLook.h"

#include "common/JPlacerLog.h"
#include "vision/JPRoundMarkFinder.h"

#include <j/core/Log.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <random>

inline namespace jf {

namespace {

// The least tolerance, however still the measuring is (mm).
constexpr double kLeastToleranceMm = 0.002;
// The tolerance is this many times the measuring's own spread.
constexpr double kToleranceSpreads = 3.0;
// Each distance tested is this many times the last.
constexpr double kDistanceStep = 1.6;
// How far from where it is expected the mark is looked for (mm): the play
// and the camera's offset error together are far less.
constexpr double kSearchMm = 1.0;

std::string now() {
    const std::time_t t = std::time(nullptr);
    char buf[32];
    std::strftime(buf, sizeof buf, "%Y-%m-%d %H:%M:%S", std::localtime(&t));
    return buf;
}

// Puts the compensation back on, whatever happens.
struct CompensationOff {
    JPCell& cell;
    explicit CompensationOff(JPCell& c) : cell(c) { cell.setBacklashCompensation(false); }
    ~CompensationOff() { cell.setBacklashCompensation(true); }
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
    const double scale = cal.scale();
    const JPAxisConfig::Backlash wasMethod = axis->backlash;
    const double wasOffset = axis->backlashOffset, wasSneak = axis->sneakUpMm, wasSpeed = axis->backlashSpeedFactor;

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
        JPGrayImage img;
        if (!JPCameraLook::settled(feed, img, r.why)) return false;
        const double viewX = alongX ? at + mount.offsetX : o.markX, viewY = alongX ? o.markY : at + mount.offsetY;
        JPRoundMarkFinder::Request rq;
        if (!cal.pixelFor(o.markX, o.markY, viewX, viewY, rq.expectedX, rq.expectedY)) {
            rq.expectedX = img.width / 2.0;
            rq.expectedY = img.height / 2.0;
        }
        rq.searchRadius = kSearchMm * scale;
        rq.diameter = o.markDiameterMm * scale;
        const JPRoundMark m = JPCameraLook::findTryingHarder(cell, feed, img, rq);
        if (!m.found) {
            r.why = "the mark was not found: " + m.why;
            return false;
        }
        double mx, my;
        if (!cal.machinePoint(m.x, m.y, viewX, viewY, mx, my)) {
            r.why = "the mark was seen where the calibration cannot place it";
            return false;
        }
        along = alongX ? mx : my;
        return true;
    };
    // In to the place from `from` (signed, mm), having come to `from` from
    // the far other side, so the last stretch reverses: the last at `last`.
    auto approach = [&](double from, double last, double& along) {
        const double side = from > 0 ? 1 : -1;
        return go(target - side * o.reachMm, o.speed) && go(target + from, o.speed) && go(target, last)
            && measure(target, along);
    };
    // The play coming in over `distance` at `last`: from below less from above.
    auto play = [&](double distance, double last, double& p) {
        double below, above;
        if (!approach(-distance, last, below) || !approach(distance, last, above)) return false;
        p = below - above;
        return true;
    };

    {
        CompensationOff off(cell);
        // 1. Still.
        if (progress) progress("measuring the mark standing still");
        if (!go(target, o.speed)) return r;
        std::vector<double> still;
        for (int i = 0; i < std::max(2, o.still); ++i) {
            double a;
            if (!measure(target, a)) return r;
            still.push_back(a);
        }
        double mean = 0, var = 0;
        for (double a : still) mean += a;
        mean /= double(still.size());
        for (double a : still) var += (a - mean) * (a - mean);
        const double spread = std::sqrt(var / double(still.size() - 1));
        const double tol = std::max({ kLeastToleranceMm, kToleranceSpreads * spread, axis->resolution });
        r.data.toleranceMm = tol;

        // 2. Against distance, at the slowest speed.
        const double slowest = kSpeeds[0];
        std::vector<double> distances;
        for (double d = std::max(2 * tol, 0.01); d < o.reachMm; d *= kDistanceStep) distances.push_back(d);
        distances.push_back(o.reachMm);
        for (size_t i = 0; i < distances.size(); ++i) {
            char said[80];
            std::snprintf(said, sizeof said, "the play coming in %.3f mm (%zu of %zu)", distances[i], i + 1, distances.size());
            if (progress) progress(said);
            double p;
            if (!play(distances[i], slowest, p)) return r;
            r.data.byDistance.push_back({ distances[i], p });
        }
        // 3. Against speed, from far.
        for (double s : kSpeeds) {
            char said[64];
            std::snprintf(said, sizeof said, "the play at %.0f%% speed", s * 100);
            if (progress) progress(said);
            double p;
            if (!play(o.reachMm, s, p)) return r;
            r.data.bySpeed.push_back({ s, p });
        }

        // 4. The method.
        const double full = r.data.bySpeed.front().second;   // from far, slowly: all the play
        double sneak = o.reachMm;                              // the shortest distance from which all of it shows
        for (auto it = r.data.byDistance.rbegin(); it != r.data.byDistance.rend() && it->second >= full - 2 * tol; ++it)
            sneak = it->first;
        bool consistent = true;
        for (const auto& [s, p] : r.data.bySpeed)
            if (std::abs(p - full) > tol) consistent = false;
        if (full < tol) {
            r.method = JPAxisConfig::Backlash::None;
        } else if (consistent) {
            r.method = JPAxisConfig::Backlash::Directional;
            r.offset = full;
        } else if (std::max(sneak, full) <= o.mostSneakUpMm) {
            r.method = JPAxisConfig::Backlash::DirectionalSneakUp;
            r.offset = full;
            r.sneakUpMm = std::max(sneak, full);
            r.speedFactor = slowest;
        } else {
            r.method = JPAxisConfig::Backlash::OneSided;
            r.offset = full + 2 * tol;   // at least the play
            r.speedFactor = slowest;
        }
    }

    // 5. Tried: in from random distances either way, against the mark's
    // place as the compensated axis finds it coming in from each side.
    cell.setBacklash(axisId, r.method, r.offset, r.sneakUpMm, r.speedFactor);
    if (progress) progress("trying it");
    double fromBelow, fromAbove;
    if (!go(target - o.reachMm, o.speed) || !go(target, o.speed) || !measure(target, fromBelow)
        || !go(target + o.reachMm, o.speed) || !go(target, o.speed) || !measure(target, fromAbove)) {
        cell.setBacklash(axisId, wasMethod, wasOffset, wasSneak, wasSpeed);
        return r;
    }
    const double reference = (fromBelow + fromAbove) / 2;
    std::mt19937 rng(std::random_device{}());
    std::uniform_real_distribution<double> logDistance(std::log(std::max(2 * r.data.toleranceMm, 0.01)), std::log(o.reachMm));
    std::bernoulli_distribution side(0.5);
    for (int i = 0; i < o.tries; ++i) {
        const double d = std::exp(logDistance(rng)) * (side(rng) ? 1 : -1);
        double a;
        if (!go(target + d, o.speed) || !go(target, o.speed) || !measure(target, a)) {
            cell.setBacklash(axisId, wasMethod, wasOffset, wasSneak, wasSpeed);
            return r;
        }
        r.data.after.push_back({ d, a - reference });
        r.worstAfterMm = std::max(r.worstAfterMm, std::abs(a - reference));
    }
    r.data.when = now();
    r.ok = true;
    JLOGC(JPlacerLog::kCell, JLogLevel::Info) << axis->name << " backlash: " << JPAxisConfig::backlashWord(r.method)
        << " offset " << r.offset << " sneak-up " << r.sneakUpMm << " speed " << r.speedFactor
        << ", tolerance " << r.data.toleranceMm << ", worst after " << r.worstAfterMm;
    return r;
}

} // inline namespace jf
