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
    const auto wasTable = axis->backlashTable;
    const double wasApproach = axis->approachMm;
    // A way to compensate, to try.
    struct Candidate {
        JPAxisConfig::Backlash method = JPAxisConfig::Backlash::None;
        double offset = 0, sneakUpMm = 0, speedFactor = 1;
        std::vector<std::pair<double, double>> table;
        double approachMm = 0;
    };
    std::vector<Candidate> candidates;

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
        // The mark in several pictures, once settled, its place the mean.
        double sumX = 0, sumY = 0;
        const int frames = std::max(1, o.frames);
        for (int f = 0; f < frames; ++f) {
            JPGrayImage img;
            if (!(f == 0 ? JPCameraLook::settled(feed, img, r.why) : JPCameraLook::taken(feed, img, r.why, 1))) return false;
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
            sumX += m.x;
            sumY += m.y;
        }
        double mx, my;
        if (!cal.machinePoint(sumX / frames, sumY / frames, viewX, viewY, mx, my)) {
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
        // The play must level off within a short sneak-up for a directional
        // method to be right. One that keeps growing with how far the axis
        // came in (a belt stretching) is made the same every time either by
        // ending every move the same way (one-sided), or by sending each move
        // the lag measured for how far it came (distance-aware): both are
        // tried, and the one that lands closer kept.
        candidates.clear();
        if (full < tol) {
            candidates.push_back({ JPAxisConfig::Backlash::None });
        } else if (std::max(sneak, full) > o.mostSneakUpMm) {
            Candidate one{ JPAxisConfig::Backlash::OneSided };
            // The same last stretch every move, from as far as the play takes
            // to level off, so what came before is taken up the same way.
            one.offset = std::max(std::min(sneak, o.longestLastMm), std::max(2 * full, full + 2 * tol));
            one.speedFactor = slowest;
            candidates.push_back(one);
            Candidate aware{ JPAxisConfig::Backlash::DistanceAware };
            aware.table = lagTable(r.data.byDistance);
            // The least approach: past the gap, the first distance whose lag is
            // no longer behind (the table never falls), else the furthest.
            aware.approachMm = aware.table.back().first;
            for (auto it = aware.table.rbegin(); it != aware.table.rend() && it->second >= 0; ++it)
                aware.approachMm = it->first;
            aware.speedFactor = slowest;
            candidates.push_back(aware);
        } else if (consistent) {
            Candidate c{ JPAxisConfig::Backlash::Directional };
            c.offset = full;
            candidates.push_back(c);
        } else {
            Candidate c{ JPAxisConfig::Backlash::DirectionalSneakUp };
            c.offset = full;
            c.sneakUpMm = std::max(sneak, full);
            c.speedFactor = slowest;
            candidates.push_back(c);
        }
    }

    // 5. Tried: in from random distances either way (and from each side from
    // afar), each against the mean of them all: how well moves agree with each
    // other, whatever the machine slowly drifts by over the run. The same
    // distances for each candidate.
    std::mt19937 rng(std::random_device{}());
    std::uniform_real_distribution<double> logDistance(std::log(std::max(2 * r.data.toleranceMm, 0.01)), std::log(o.reachMm));
    std::bernoulli_distribution side(0.5);
    std::vector<double> distances = { -o.reachMm, o.reachMm };
    for (int i = 0; i < o.tries; ++i) distances.push_back(std::exp(logDistance(rng)) * (side(rng) ? 1 : -1));
    double best = -1;
    for (const Candidate& c : candidates) {
        cell.setBacklash(axisId, c.method, c.offset, c.sneakUpMm, c.speedFactor, c.table, c.approachMm);
        if (progress) progress(std::string("trying ") + JPAxisConfig::backlashWord(c.method));
        std::vector<std::pair<double, double>> tried;
        for (double d : distances) {
            double a;
            if (!go(target + d, o.speed) || !go(target, o.speed) || !measure(target, a)) {
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
        JLOGC(JPlacerLog::kCell, JLogLevel::Info) << axis->name << " backlash " << JPAxisConfig::backlashWord(c.method)
                                                  << ": worst " << worst;
        if (best >= 0 && worst >= best) continue;
        best = worst;
        r.method = c.method;
        r.offset = c.offset;
        r.sneakUpMm = c.sneakUpMm;
        r.speedFactor = c.speedFactor;
        r.table = c.table;
        r.approachMm = c.approachMm;
        r.worstAfterMm = worst;
        r.data.after.clear();
        for (const auto& [d, a] : tried) r.data.after.push_back({ d, a - mean });
    }
    // In use: the one kept.
    cell.setBacklash(axisId, r.method, r.offset, r.sneakUpMm, r.speedFactor, r.table, r.approachMm);
    r.data.when = now();
    r.ok = true;
    JLOGC(JPlacerLog::kCell, JLogLevel::Info) << axis->name << " backlash: " << JPAxisConfig::backlashWord(r.method)
        << " offset " << r.offset << " sneak-up " << r.sneakUpMm << " approach " << r.approachMm << " speed " << r.speedFactor
        << ", tolerance " << r.data.toleranceMm << ", worst after " << r.worstAfterMm;
    return r;
}

} // inline namespace jf
