// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// Defaults, then Auto-Tune, against a stand-in camera: every setting to the
// camera's default, the automatic ones left to it for their moment, then
// switched to manual (the camera holding what it settled on), and those held
// values kept by hand; a camera with no settings of its own: nothing to tune.
// Tests check with assert(); a Release build must not compile it away.
#undef NDEBUG
#include <cassert>

#include "camera/JPAutoTune.h"

using namespace jf;

namespace {

// A camera with exposure (automatic, settling at 333 while auto) and brightness (manual only).
class StandIn : public JPCaptureSource {
public:
    bool exposureAuto = false;
    int  exposure = 50, brightness = 10;
    int  sets = 0;
    bool open(std::string&) override { return true; }
    void close() override {}
    std::vector<JPCaptureMode> modes() const override { return {}; }
    bool start(const JPCaptureMode&, std::string&) override { return true; }
    bool grab(JPFrame&, int, std::string&) override { return false; }
    std::string describe() const override { return "stand-in"; }
    JJson controls() const override {
        JJson c = JJson::object();
        c["exposure"]["value"] = exposure;
        c["exposure"]["default"] = 100;
        c["exposure"]["auto"] = exposureAuto;
        c["brightness"]["value"] = brightness;
        c["brightness"]["default"] = 128;
        return c;
    }
    void setControls(const JJson& c) override {
        ++sets;
        if (c["exposure"]["auto"].isBool()) exposureAuto = c["exposure"]["auto"].boolean();
        if (exposureAuto) exposure = 333;   // the camera's own choice
        else if (c["exposure"]["value"].isNumber()) exposure = int(c["exposure"]["value"].number());
        if (c["brightness"]["value"].isNumber()) brightness = int(c["brightness"]["value"].number());
    }
};

class Bare : public StandIn {
public:
    JJson controls() const override { return JJson::object(); }
};

} // namespace

int main() {
    using Clock = JPAutoTune::Clock;
    const Clock::time_point t0 = Clock::now();
    StandIn cam;
    JPAutoTune tune(1500, 200);
    assert(tune.start(cam, t0));
    // Defaults: brightness to its default; exposure left automatic.
    assert(cam.brightness == 128 && cam.exposureAuto && cam.exposure == 333);
    assert(!tune.step(cam, t0 + std::chrono::milliseconds(1000)));        // still tuning
    assert(!tune.step(cam, t0 + std::chrono::milliseconds(1500)));        // switched to manual
    assert(!cam.exposureAuto && cam.exposure == 333);                     // holding what it settled on
    assert(!tune.step(cam, t0 + std::chrono::milliseconds(1600)));        // holding
    const auto tuned = tune.step(cam, t0 + std::chrono::milliseconds(1700));
    assert(tuned && !(*tuned)["exposure"]["auto"].boolean() && (*tuned)["exposure"]["value"].number() == 333);
    assert((*tuned)["brightness"]["value"].number() == 128 && !(*tuned)["brightness"]["auto"].boolean());
    assert(!tune.step(cam, t0 + std::chrono::milliseconds(5000)));        // done once
    // No settings of its own: nothing to tune.
    Bare bare;
    JPAutoTune none(1500, 200);
    assert(!none.start(bare, t0) && bare.sets == 0);
    return 0;
}
