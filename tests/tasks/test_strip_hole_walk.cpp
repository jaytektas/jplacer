// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// Strip Auto Setup's walk down the tape, over a 10 part strip a little askew, each look a few hundredths off: from
// the reference and next holes 4 mm apart, a look's 0.065 mm went into every part (OpenPnP stretches the part
// pitch to how far apart they are); followed to the hole by the last part, 9 on, it is shared among them. The
// camera's view half as long as the strip walks it in steps; a strip shorter than its parts stops at its end.
// Tests check with assert(); a Release build must not compile it away.
#undef NDEBUG
#include <cassert>

#include "FakeJobMachine.h"

#include "pipeline/JPPipeline.h"
#include "tasks/JPStripHoleWalk.h"

#include <cmath>

using namespace jf;

namespace {

constexpr double kPxPerMm = 25.5, kAskewDeg = 0.4, kPitch = 4, kLookOffMm = 0.0325;
constexpr int kParts = 10;
using L = JPLocation;
constexpr JPLengthUnit kMm = JPLengthUnit::Millimeters;

// The strip's holes, fed towards -Y turned a little, `holes` of them; its parts 3.5 mm across and 2 mm along from each.
struct Strip {
    int    holes = kParts + 1;
    double ux = std::sin(kAskewDeg * M_PI / 180), uy = -std::cos(kAskewDeg * M_PI / 180);
    double x0 = 92.5, y0 = 36.6;
    L hole(int k) const { return L(kMm, x0 + ux * kPitch * k, y0 + uy * kPitch * k, 0, 0); }
    L part(int k) const {
        const L h = hole(k);
        return L(kMm, h.x() - uy * 3.5 + ux * 2, h.y() + ux * 3.5 + uy * 2, 0, 0);
    }
};

// Sees the strip's holes in a 1280 x 720 picture from where it is put, each look off along the tape, one way then the other.
class TapeCamera : public FakeJobMachine {
public:
    explicit TapeCamera(const Strip& s) : m_strip(s) {}
    int looks = 0;
    bool positionCamera(const L& at, std::string&) override {
        m_at = at;
        return true;
    }
    bool seeCircles(const L&, JPPipeline&, SeenCircles& seen, std::string&) override {
        seen = {};
        seen.centreX = 640;
        seen.centreY = 360;
        seen.pixelsPerMm = kPxPerMm;
        const double off = (looks++ % 2 ? kLookOffMm : -kLookOffMm) * kPxPerMm;
        for (int k = 0; k < m_strip.holes; ++k) {
            const L h = m_strip.hole(k);
            const double px = 640 + (h.x() - m_at.x()) * kPxPerMm, py = 360 - (h.y() - m_at.y()) * kPxPerMm + off;
            if (px > 20 && px < 1260 && py > 20 && py < 700) seen.circles.push_back({ px, py, 1.5 * kPxPerMm });
        }
        const L at = m_at;
        seen.toMachine = [at](double px, double py, double& x, double& y) {
            x = at.x() + (px - 640) / kPxPerMm;
            y = at.y() - (py - 360) / kPxPerMm;
            return true;
        };
        return true;
    }

private:
    const Strip& m_strip;
    L            m_at { kMm };
};

double apart(const L& a, const L& b) { return std::hypot(b.x() - a.x(), b.y() - a.y()); }
double angleOf(const L& a, const L& b) { return std::atan2(b.y() - a.y(), b.x() - a.x()) * 180 / M_PI; }

// The reference and next holes as the clicks found them: the next one a look's 0.065 mm too far.
const Strip kStrip;
const L kReference = kStrip.hole(0);
const L kNext(kMm, kStrip.hole(1).x() + kStrip.ux * 2 * kLookOffMm, kStrip.hole(1).y() + kStrip.uy * 2 * kLookOffMm, 0, 0);
// Half the camera's 720 px side.
const double kStepMm = 720 / kPxPerMm / 2;

} // namespace

int main() {
    JPPipeline pipeline;
    const double trueAngle = angleOf(kStrip.hole(0), kStrip.hole(1));
    assert(std::abs(apart(kReference, kNext) - (kPitch + 2 * kLookOffMm)) < 1e-9);   // as it was: 4.065

    // Followed to the 10th part's hole, 9 on, in steps of 3 holes (half a 28 mm view).
    {
        TapeCamera camera(kStrip);
        int lastAsked = 0;
        const auto w = JPStripHoleWalk::walk(camera, pipeline, 8, kStrip.part(0), kReference, kNext, 9, kStepMm,
                                             [&](int hole, int of) { assert(of == 9 && hole > lastAsked); lastAsked = hole; });
        assert(w.reached && w.holes == 9 && lastAsked == 9);
        assert(camera.looks == 3);
        // A ninth of the look's error in each hole's pitch; the 10th part within one look of where it is.
        assert(std::abs(apart(kReference, w.last) / 9 - kPitch) < 2 * kLookOffMm / 9 + 1e-6);
        assert(std::abs(apart(w.last, kStrip.hole(9))) < kLookOffMm + 1e-6);
        assert(std::abs(angleOf(kReference, w.last) - trueAngle) < 0.06);
    }

    // Nothing to follow (no Max Feed Count): the next hole kept.
    {
        TapeCamera camera(kStrip);
        const auto w = JPStripHoleWalk::walk(camera, pipeline, 8, kStrip.part(0), kReference, kNext, 0, kStepMm, nullptr);
        assert(w.reached && w.holes == 1 && camera.looks == 0);
        assert(apart(w.last, kNext) == 0);
    }

    // A strip shorter than its count (5 holes): followed to its last hole, and said so.
    {
        Strip shorter = kStrip;
        shorter.holes = 5;
        TapeCamera camera(shorter);
        const auto w = JPStripHoleWalk::walk(camera, pipeline, 8, kStrip.part(0), kReference, kNext, 9, kStepMm, nullptr);
        assert(!w.reached && w.holes == 4);
        assert(apart(w.last, shorter.hole(4)) < kLookOffMm + 1e-6);
    }
    return 0;
}
