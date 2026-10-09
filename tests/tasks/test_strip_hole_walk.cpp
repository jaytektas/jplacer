// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// Strip Auto Setup over a 10 part strip a little askew, seen by a camera calibrated at another height: the tape
// 1.2% smaller in its pictures than the calibration says (as on the bench: 25.2 px/mm against 25.52), each mark's
// centre found to the half pixel. A hole seen off the picture's middle is then off by 1.2% of how far, and each
// look sees it elsewhere: two holes 4 mm apart, in two pictures, came out 4.065 apart, and OpenPnP stretches the
// part pitch to that. The tape's scale measured by moving the camera, the holes found at it, and followed down
// the strip to the one by the last part, the pitch and the tape's angle are true. And a camera calibrated at two
// heights gives back the height a scale is seen at.
// Tests check with assert(); a Release build must not compile it away.
#undef NDEBUG
#include <cassert>

#include "FakeJobMachine.h"

#include "machine/JPCameraCalibration.h"
#include "pipeline/JPPipeline.h"
#include "tasks/JPStripHoleWalk.h"

#include <cmath>

using namespace jf;

namespace {

constexpr double kCalibratedPxPerMm = 25.52, kTapePxPerMm = 25.2, kAskewDeg = 0.4, kPitch = 4;
// The strip not level: by its 10th part (36 mm on) the tape is seen at this scale (higher: bigger).
constexpr double kLastTapePxPerMm = 25.32;
constexpr int kParts = 10;
using L = JPLocation;
constexpr JPLengthUnit kMm = JPLengthUnit::Millimeters;

// The strip's holes, fed towards -Y turned a little: the tape's leader before the first part, `holes` from it on;
// its parts 3.5 mm across and 2 mm along from each.
struct Strip {
    int    leader = 3, holes = kParts + 1;
    double ux = std::sin(kAskewDeg * M_PI / 180), uy = -std::cos(kAskewDeg * M_PI / 180);
    double x0 = 92.5, y0 = 36.6;
    L hole(int k) const { return L(kMm, x0 + ux * kPitch * k, y0 + uy * kPitch * k, 0, 0); }
    L part(int k) const {
        const L h = hole(k);
        return L(kMm, h.x() - uy * 3.5 + ux * 2, h.y() + ux * 3.5 + uy * 2, 0, 0);
    }
};

// A 1280 x 720 picture of the strip's holes at the tape's scale, centres to the half pixel; turned back to the
// machine at the calibrated scale.
class TapeCamera : public FakeJobMachine {
public:
    explicit TapeCamera(const Strip& s, bool level = true) : m_strip(s), m_level(level) {}
    int looks = 0;
    double leastY = -1e9;   // a soft limit: the camera goes no lower in Y
    bool cameraReaches(const L& at) const override { return at.y() >= leastY; }
    // The scale the tape is seen at under the camera: level, the first part's; else its share towards the last's.
    double tapeScale() const {
        if (m_level) return kTapePxPerMm;
        const L h0 = m_strip.hole(0);
        const double along = (m_at.x() - h0.x()) * m_strip.ux + (m_at.y() - h0.y()) * m_strip.uy;
        return kTapePxPerMm + (kLastTapePxPerMm - kTapePxPerMm) * along / (9 * kPitch);
    }
    bool positionCamera(const L& at, std::string&) override {
        m_at = at;
        return true;
    }
    bool seeCircles(const L&, JPPipeline&, SeenCircles& seen, std::string&) override {
        ++looks;
        seen = {};
        seen.centreX = 640;
        seen.centreY = 360;
        seen.pixelsPerMm = kCalibratedPxPerMm;
        auto half = [](double p) { return std::floor(p) + 0.5; };
        const double tape = tapeScale();
        for (int k = -m_strip.leader; k < m_strip.holes; ++k) {
            const L h = m_strip.hole(k);
            const double px = half(640 + (h.x() - m_at.x()) * tape), py = half(360 - (h.y() - m_at.y()) * tape);
            if (px > 20 && px < 1260 && py > 20 && py < 700) seen.circles.push_back({ px, py, 1.5 * tape });
        }
        const L at = m_at;
        seen.toMachine = [at](double px, double py, double& x, double& y) {
            x = at.x() + (px - 640) / kCalibratedPxPerMm;
            y = at.y() - (py - 360) / kCalibratedPxPerMm;
            return true;
        };
        return true;
    }

private:
    const Strip& m_strip;
    bool         m_level;
    L            m_at { kMm };
};

double apart(const L& a, const L& b) { return std::hypot(b.x() - a.x(), b.y() - a.y()); }
double angleOf(const L& a, const L& b) { return std::atan2(b.y() - a.y(), b.x() - a.x()) * 180 / M_PI; }

const Strip kStrip;
// Half the camera's 720 px side.
const double kStepMm = 720 / kCalibratedPxPerMm / 2;

// The hole nearest `near` among those seen from `from`, at the scale `pxPerMm` (0: the calibrated one).
L holeSeen(TapeCamera& camera, JPPipeline& p, const L& from, const L& near, double pxPerMm) {
    std::string why;
    JPJobMachine::SeenCircles seen;
    camera.positionCamera(from, why);
    camera.seeCircles(from, p, seen, why);
    JPStripHoleWalk::atScale(seen, pxPerMm);
    const auto holes = JPStripHoleWalk::holesOf(seen, 8);
    assert(!holes.empty());
    const L* best = &holes.front();
    for (const L& h : holes) if (apart(h, near) < apart(*best, near)) best = &h;
    return *best;
}

} // namespace

int main() {
    JPPipeline pipeline;
    const double trueAngle = angleOf(kStrip.hole(0), kStrip.hole(1));

    // At the calibrated scale, the same hole seen from 6 mm either side along the tape: off, and elsewhere each time.
    {
        TapeCamera camera(kStrip);
        const L h0 = kStrip.hole(0), p0 = kStrip.part(0);
        const L a = holeSeen(camera, pipeline, L(kMm, p0.x(), p0.y() - 6, 0, 0), h0, 0);
        const L b = holeSeen(camera, pipeline, L(kMm, p0.x(), p0.y() + 6, 0, 0), h0, 0);
        assert(apart(a, h0) > 0.04 && apart(b, h0) > 0.04 && apart(a, b) > 0.02);
    }

    // The tape's scale, from the camera moved 5 mm either way along X and Y: within a fifth of a percent.
    TapeCamera camera(kStrip);
    std::string why;
    const auto scale = JPStripHoleWalk::scaleAt(camera, pipeline, kStrip.part(0), 5, why);
    assert(scale && std::abs(*scale / kTapePxPerMm - 1) < 0.002);
    assert(camera.looks == 4);

    // At it, the same hole from 6 mm either side: where it is, to the half pixel.
    {
        const L h0 = kStrip.hole(0), p0 = kStrip.part(0);
        const L a = holeSeen(camera, pipeline, L(kMm, p0.x(), p0.y() - 6, 0, 0), h0, *scale);
        const L b = holeSeen(camera, pipeline, L(kMm, p0.x(), p0.y() + 6, 0, 0), h0, *scale);
        assert(apart(a, h0) < 0.02 && apart(b, h0) < 0.02);
    }

    // The reference and next holes, from the first and second parts at the tape's scale; then followed to the
    // 10th part's hole, 9 on, in steps of 3 holes (half a 28 mm view).
    {
        const L reference = holeSeen(camera, pipeline, kStrip.part(0), kStrip.hole(0), *scale);
        const L next = holeSeen(camera, pipeline, kStrip.part(1), kStrip.hole(1), *scale);
        assert(apart(reference, kStrip.hole(0)) < 0.03 && apart(next, kStrip.hole(1)) < 0.03);
        camera.looks = 0;
        int lastAsked = 0;
        const auto w = JPStripHoleWalk::walk(camera, pipeline, 8, kStrip.part(0), reference, next, 9, kStepMm, *scale, *scale,
                                             [&](int hole, int of) { assert(of == 9 && hole > lastAsked); lastAsked = hole; });
        assert(w.reached && w.holes == 9 && lastAsked == 9 && camera.looks == 3);
        assert(apart(w.last, kStrip.hole(9)) < 0.03);
        assert(std::abs(apart(reference, w.last) / 9 - kPitch) < 0.007);
        assert(std::abs(angleOf(reference, w.last) - trueAngle) < 0.1);

        // Nothing to follow (no Max Feed Count): the next hole kept.
        const auto none = JPStripHoleWalk::walk(camera, pipeline, 8, kStrip.part(0), reference, next, 0, kStepMm, *scale, *scale, nullptr);
        assert(none.reached && none.holes == 1 && apart(none.last, next) == 0);

        // A strip shorter than its count (5 holes): followed to its last hole, and said so.
        Strip shorter = kStrip;
        shorter.holes = 5;
        TapeCamera shortCamera(shorter);
        const auto w2 = JPStripHoleWalk::walk(shortCamera, pipeline, 8, kStrip.part(0), reference, next, 9, kStepMm, *scale, *scale, nullptr);
        assert(!w2.reached && w2.holes == 4 && apart(w2.last, shorter.hole(4)) < 0.03);
    }

    // A strip not level, seen 0.5% bigger by its last part: the scale measured there too (where the camera looks
    // from by the hole 9 on, as by the first), and the walk at each look's share between them.
    {
        TapeCamera tilted(kStrip, false);
        const auto first = JPStripHoleWalk::scaleAt(tilted, pipeline, kStrip.part(0), 5, why);
        assert(first && std::abs(*first / kTapePxPerMm - 1) < 0.002);
        const L reference = holeSeen(tilted, pipeline, kStrip.part(0), kStrip.hole(0), *first);
        const L next = holeSeen(tilted, pipeline, kStrip.part(1), kStrip.hole(1), *first);
        const L beside = JPStripHoleWalk::besideHole(kStrip.part(0), reference, next, 9);
        assert(apart(beside, kStrip.part(9)) < 0.3);
        const auto last = JPStripHoleWalk::scaleAt(tilted, pipeline, beside, 5, why);
        assert(last && std::abs(*last / kLastTapePxPerMm - 1) < 0.002);
        const auto w = JPStripHoleWalk::walk(tilted, pipeline, 8, kStrip.part(0), reference, next, 9, kStepMm, *first, *last, nullptr);
        assert(w.reached && w.holes == 9 && apart(w.last, kStrip.hole(9)) < 0.03);
        // The strip's end by a soft limit (as on the bench, 2.6 mm from Y 0): measured twice as far the other way.
        tilted.leastY = beside.y() - 2.6;
        tilted.looks = 0;
        const auto nearLimit = JPStripHoleWalk::scaleAt(tilted, pipeline, beside, 5, why);
        assert(nearLimit && tilted.looks == 4 && std::abs(*nearLimit / kLastTapePxPerMm - 1) < 0.003);
    }

    // A camera calibrated at two heights (Z -23.2 and -12.1, 113 mm above the first): the height a scale is seen
    // at, back from the scale there; with one height, its own.
    {
        JPCameraCalibration cal;
        cal.valid = true;
        cal.pxPerMm = { -26.14, 0, 0, 26.14 };
        cal.z = -23.2;
        cal.secondZ = -12.1;
        cal.secondScale = 26.14 * 113 / (113 - 11.1);
        assert(cal.twoHeights());
        for (const double z : { -26.0, -24.4, -23.2, -20.0, -12.1 }) assert(std::abs(cal.heightAt(cal.scaleAt(z)) - z) < 1e-9);
        assert(std::abs(cal.heightAt(cal.scale() * 0.988) - (-23.2 - 113 * (1 / 0.988 - 1))) < 1e-6);
        JPCameraCalibration one = cal;
        one.secondScale = 0;
        assert(one.heightAt(20) == one.z);
    }
    return 0;
}
