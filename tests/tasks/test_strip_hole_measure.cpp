// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// Auto Setup's strip holes measured for the tape's angle and pitch, over a 10 part strip a little askew: a camera
// whose scale is 1.6% off at the tape's height (as one calibrated at another), and each look off by a few
// hundredths (settling, a tenth of a pixel). From the reference and next holes, 4 mm apart, one look's 0.065 mm
// went into every part's pitch (0.58 mm by the 10th part); with the last hole by the strip's last part, it is
// shared among 9 holes. Each hole is looked at from the same place beside it, so the scale moves both alike.
// Tests check with assert(); a Release build must not compile it away.
#undef NDEBUG
#include <cassert>

#include "FakeJobMachine.h"

#include "pipeline/JPPipeline.h"
#include "tasks/JPStripHoleMeasure.h"

#include <cmath>

using namespace jf;

namespace {

constexpr double kPxPerMm = 26, kScaleOff = 1.016, kAskewDeg = 0.4, kPitch = 4, kLookOffMm = 0.0325;
constexpr int kParts = 10;

// The strip's holes, fed towards -Y turned a little; its parts 3.5 mm across and 2 mm along from each.
struct Strip {
    double ux = std::sin(kAskewDeg * M_PI / 180), uy = -std::cos(kAskewDeg * M_PI / 180);
    double x0 = 92.5, y0 = 36.6;
    JPLocation hole(int k) const { return JPLocation(JPLengthUnit::Millimeters, x0 + ux * kPitch * k, y0 + uy * kPitch * k, 0, 0); }
    JPLocation part(int k) const {
        const JPLocation h = hole(k);
        return JPLocation(JPLengthUnit::Millimeters, h.x() - uy * 3.5 + ux * 2, h.y() + ux * 3.5 + uy * 2, 0, 0);
    }
};

// Sees the strip's holes from where it is put, its pixels turned back to the machine at a scale 1.6% off.
class TapeCamera : public FakeJobMachine {
public:
    explicit TapeCamera(const Strip& s) : m_strip(s) {}
    bool positionCamera(const JPLocation& at, std::string&) override {
        m_at = at;
        return true;
    }
    bool seeCircles(const JPLocation&, JPPipeline&, SeenCircles& seen, std::string&) override {
        seen = {};
        // Each look a little off along the tape, one way then the other.
        const double off = (m_looks++ % 2 ? kLookOffMm : -kLookOffMm) * kPxPerMm;
        seen.centreX = 640;
        seen.centreY = 360;
        seen.pixelsPerMm = kPxPerMm;
        for (int k = -3; k < kParts + 3; ++k) {
            const JPLocation h = m_strip.hole(k);
            const double px = 640 + (h.x() - m_at.x()) * kPxPerMm * kScaleOff, py = 360 - (h.y() - m_at.y()) * kPxPerMm * kScaleOff + off;
            if (px > 0 && px < 1280 && py > 0 && py < 720) seen.circles.push_back({ px, py, 1.5 * kPxPerMm });
        }
        const JPLocation at = m_at;
        seen.toMachine = [at](double px, double py, double& x, double& y) {
            x = at.x() + (px - 640) / kPxPerMm;
            y = at.y() - (py - 360) / kPxPerMm;
            return true;
        };
        return true;
    }

private:
    const Strip& m_strip;
    JPLocation   m_at { JPLengthUnit::Millimeters };
    int          m_looks = 0;
};

// As the camera saw a hole from `from` (scale only).
JPLocation seenFrom(const JPLocation& from, const JPLocation& hole) {
    return JPLocation(JPLengthUnit::Millimeters, from.x() + (hole.x() - from.x()) * kScaleOff, from.y() + (hole.y() - from.y()) * kScaleOff, 0, 0);
}

double apart(const JPLocation& a, const JPLocation& b) { return std::hypot(b.x() - a.x(), b.y() - a.y()); }
double angleOf(const JPLocation& a, const JPLocation& b) { return std::atan2(b.y() - a.y(), b.x() - a.x()) * 180 / M_PI; }

} // namespace

int main() {
    const Strip strip;
    const double trueAngle = angleOf(strip.hole(0), strip.hole(1));
    // The holes as the clicks found them: the hole ahead of each part (OpenPnP's choice), seen from it.
    const JPLocation first = strip.part(0), second = strip.part(1);
    const JPLocation found1 = seenFrom(first, strip.hole(1)), found2 = seenFrom(second, strip.hole(2));
    JPPipeline pipeline;
    std::string note;

    // Not counted: the next hole, each look's error all in the pitch (as it was).
    {
        TapeCamera camera(strip);
        JPLocation ref1 = found1, ref2 = found2;
        JPStripHoleMeasure::measure(camera, pipeline, 8, first, 0, kPitch, ref1, ref2, note);
        assert(note.empty());
        assert(std::abs(apart(ref1, ref2) - kPitch) < 2 * kLookOffMm + 1e-3);
    }

    // Counted: the last hole by the 10th part, 9 holes on: the angle and pitch true to a ninth of it.
    {
        TapeCamera camera(strip);
        JPLocation ref1 = found1, ref2 = found2;
        JPStripHoleMeasure::measure(camera, pipeline, 8, first, kParts, kPitch, ref1, ref2, note);
        assert(note.empty());
        const double holes = std::round(apart(ref1, ref2) / kPitch);
        assert(holes == 9);
        assert(std::abs(apart(ref1, ref2) / holes - kPitch) < 2 * kLookOffMm / 9 + 1e-3);
        assert(std::abs(angleOf(ref1, ref2) - trueAngle) < 0.15);
        // The 10th part off by no more than one look, not nine pitches' worth.
        assert(std::abs(apart(ref1, ref2) - 9 * kPitch) < 2 * kLookOffMm + 1e-3);
    }

    // Counted past the strip's end (no hole there): the next hole kept, and said so.
    {
        TapeCamera camera(strip);
        JPLocation ref1 = found1, ref2 = found2;
        note.clear();
        JPStripHoleMeasure::measure(camera, pipeline, 8, first, 40, kPitch, ref1, ref2, note);
        assert(!note.empty());
        assert(std::abs(apart(ref1, ref2) - kPitch) < 2 * kLookOffMm + 1e-3);
    }
    return 0;
}
