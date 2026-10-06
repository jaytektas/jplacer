// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// The exposure set for a picture of the brightness wanted, against a stand-in
// camera whose picture grows as bright as its exposure is long and shows a
// new exposure only some pictures later (as the bench's does, with pictures
// queued): from dark, found in a few tries; already right, left as it is;
// from too bright to scale from, brought down first; too dark even at its
// longest, said so; a camera with no exposure: refused.
// Tests check with assert(); a Release build must not compile it away.
#undef NDEBUG
#include <cassert>
#include <cmath>
#include <deque>

#include "camera/JPOneShotExposure.h"

using namespace jf;

namespace {

// Exposure 1..5000, by hand; brightness `light` per unit of exposure, up to 255; a new exposure seen kLag
// pictures after it is set.
class StandIn : public JPCaptureSource {
public:
    static constexpr int kLag = 5;
    double light = 102.0 / 3000;
    double exposure = 52;
    bool   hasExposure = true;
    std::deque<double> shown = std::deque<double>(kLag, 52);   // what the next pictures were exposed with
    bool open(std::string&) override { return true; }
    void close() override {}
    std::vector<JPCaptureMode> modes() const override { return {}; }
    bool start(const JPCaptureMode&, std::string&) override { return true; }
    bool grab(JPFrame&, int, std::string&) override { return false; }
    std::string describe() const override { return "stand-in"; }
    JJson controls() const override {
        JJson c = JJson::object();
        if (!hasExposure) return c;
        c["exposure"]["value"] = exposure;
        c["exposure"]["min"] = 1;
        c["exposure"]["max"] = 5000;
        c["exposure"]["auto"] = false;
        return c;
    }
    void setControls(const JJson& c) override {
        if (c["exposure"]["value"].isNumber()) exposure = c["exposure"]["value"].number();
    }
    JPFrame picture() {
        shown.push_back(exposure);
        const double e = shown.front();
        shown.pop_front();
        JPFrame f;
        f.width = 64;
        f.height = 48;
        f.rgba.assign(size_t(f.width * f.height * 4), uint8_t(std::min(255.0, std::round(light * e))));
        return f;
    }
};

JPOneShotExposure::Result run(StandIn& cam, double target) {
    JPOneShotExposure x;
    std::string why;
    assert(x.start(cam, target, why));
    for (int i = 0; i < 200; ++i) {
        if (const auto r = x.step(cam)) return *r;
        x.see(cam.picture());
    }
    assert(false);
    return {};
}

void primed(StandIn& cam) {
    cam.shown.assign(StandIn::kLag, cam.exposure);
}

} // namespace

int main() {
    // From dark (the bench's 52, brightness 2): near 128 in a few tries.
    StandIn cam;
    JPOneShotExposure::Result r = run(cam, 128);
    assert(r.ok && std::abs(r.brightness - 128) <= JPOneShotExposure::kNearLevels);
    assert(std::abs(cam.exposure - 128 / cam.light) < 200 && r.pictures <= 3 * 6);
    // Already right: looked at once, left as it is.
    const double kept = cam.exposure;
    primed(cam);
    r = run(cam, 128);
    assert(r.ok && cam.exposure == kept && r.pictures == 6);
    // Too bright to scale from (white at its longest): brought down a step at a time, then scaled.
    cam.light = 0.2;
    cam.exposure = 5000;
    primed(cam);
    r = run(cam, 128);
    assert(r.ok && std::abs(r.brightness - 128) <= JPOneShotExposure::kNearLevels);
    // Too dark even at its longest: not reached, and said so.
    cam.light = 0.001;
    primed(cam);
    r = run(cam, 128);
    assert(!r.ok && cam.exposure == 5000 && r.why.find("as far as it goes") != std::string::npos);
    // No exposure of its own: refused.
    StandIn bare;
    bare.hasExposure = false;
    JPOneShotExposure none;
    std::string why;
    assert(!none.start(bare, 128, why) && !why.empty());
    return 0;
}
