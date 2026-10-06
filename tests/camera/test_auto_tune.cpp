// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// Defaults, then Auto-Tune, against a stand-in camera that, like many, does
// not say what its automatic exposure and white balance settled on (it reads
// back the last value set by hand): every setting to the camera's default,
// the automatic ones left to it for their moment while its pictures are
// looked at, then exposure and white balance found from the picture (the
// values giving the picture it gave by itself), held and kept by hand; a
// camera with no settings of its own: nothing to tune.
// Tests check with assert(); a Release build must not compile it away.
#undef NDEBUG
#include <cassert>
#include <cmath>

#include "camera/JPAutoTune.h"

using namespace jf;

namespace {

// Exposure 1..5000 (automatic, settling at 333), white balance 2800..6500
// (automatic, settling at 4600), brightness (manual only). Its picture: as
// bright as its exposure (on a log scale), as warm as its white balance is low.
class StandIn : public JPCaptureSource {
public:
    bool   exposureAuto = false, balanceAuto = false;
    double exposure = 50, balance = 2800;   // as set by hand: what it reads back
    int    brightness = 10;
    int    sets = 0;
    bool open(std::string&) override { return true; }
    void close() override {}
    std::vector<JPCaptureMode> modes() const override { return {}; }
    bool start(const JPCaptureMode&, std::string&) override { return true; }
    bool grab(JPFrame&, int, std::string&) override { return false; }
    std::string describe() const override { return "stand-in"; }
    JJson controls() const override {
        JJson c = JJson::object();
        auto one = [&c](const char* name, double value, double min, double max, double def, bool isAuto) {
            c[name]["value"] = value;
            c[name]["min"] = min;
            c[name]["max"] = max;
            c[name]["default"] = def;
            c[name]["auto"] = isAuto;
        };
        one("exposure", exposure, 1, 5000, 156, exposureAuto);
        one("white-balance", balance, 2800, 6500, 4600, balanceAuto);
        c["brightness"]["value"] = brightness;
        c["brightness"]["default"] = 128;
        return c;
    }
    void setControls(const JJson& c) override {
        ++sets;
        if (c["exposure"]["auto"].isBool()) exposureAuto = c["exposure"]["auto"].boolean();
        if (!exposureAuto && c["exposure"]["value"].isNumber()) exposure = c["exposure"]["value"].number();
        if (c["white-balance"]["auto"].isBool()) balanceAuto = c["white-balance"]["auto"].boolean();
        if (!balanceAuto && c["white-balance"]["value"].isNumber()) balance = c["white-balance"]["value"].number();
        if (c["brightness"]["value"].isNumber()) brightness = int(c["brightness"]["value"].number());
    }
    // Its picture now: grey of a brightness from the exposure in use, tinted by the white balance in use.
    JPFrame picture() const {
        const double e = exposureAuto ? 333 : exposure, w = balanceAuto ? 4600 : balance;
        const double grey = std::clamp(25 * std::log2(e), 0.0, 255.0);
        const double tint = (6500 - w) / 3700 * 60;   // warmer when lower
        JPFrame f;
        f.width = 64;
        f.height = 48;
        f.rgba.assign(size_t(f.width * f.height * 4), 255);
        for (size_t i = 0; i < f.rgba.size(); i += 4) {
            f.rgba[i] = uint8_t(std::clamp(grey + tint, 0.0, 255.0));
            f.rgba[i + 1] = uint8_t(grey);
            f.rgba[i + 2] = uint8_t(std::clamp(grey - tint, 0.0, 255.0));
        }
        return f;
    }
};

class Bare : public StandIn {
public:
    JJson controls() const override { return JJson::object(); }
};

} // namespace

int main() {
    using Clock = JPAutoTune::Clock;
    using std::chrono::milliseconds;
    Clock::time_point t = Clock::now();
    StandIn cam;
    JPAutoTune tune(1200, 200);
    assert(tune.start(cam, t));
    // Defaults: brightness to its default; exposure and white balance left automatic.
    assert(cam.brightness == 128 && cam.exposureAuto && cam.balanceAuto);
    // Run as the camera's thread does: a picture every 33 ms, a step between.
    std::optional<JJson> tuned;
    for (int i = 0; i < 200 && !tuned; ++i) {
        t += milliseconds(33);
        tune.see(cam.picture(), t);
        tuned = tune.step(cam, t);
    }
    assert(tuned);
    // Found from the picture, though the camera never said them: near what it settled on by itself.
    const double exposure = (*tuned)["exposure"]["value"].number(), balance = (*tuned)["white-balance"]["value"].number();
    assert(!(*tuned)["exposure"]["auto"].boolean() && !(*tuned)["white-balance"]["auto"].boolean());
    assert(std::abs(std::log2(exposure / 333)) < 0.2);
    assert(std::abs(balance - 4600) < 150);
    assert((*tuned)["brightness"]["value"].number() == 128 && !(*tuned)["brightness"]["auto"].boolean());
    assert(!cam.exposureAuto && !cam.balanceAuto);   // held by hand
    // Done in about two seconds of pictures.
    assert(!tune.step(cam, t + milliseconds(5000)));   // done once
    // No settings of its own: nothing to tune.
    Bare bare;
    JPAutoTune none(1200, 200);
    assert(!none.start(bare, t) && bare.sets == 0);
    return 0;
}
