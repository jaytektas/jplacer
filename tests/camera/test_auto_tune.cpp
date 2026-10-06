// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// Defaults, then Auto-Tune, against a stand-in camera that, like many, does
// not say what its automatic exposure and white balance settled on (it reads
// back the last value set by hand): every setting to the camera's default,
// the automatic ones left to it until its picture holds steady (its automatic
// exposure coming round slowly from far off), then exposure and white balance
// found from the picture (the values giving the picture it gave by itself),
// held and kept by hand; values that do not give that picture: not reached; a
// camera with no settings of its own: nothing to tune.
// Tests check with assert(); a Release build must not compile it away.
#undef NDEBUG
#include <cassert>
#include <cmath>

#include "camera/JPAutoTune.h"

using namespace jf;

namespace {

// Exposure 1..5000 (automatic, settling at 333 slowly: a twentieth of the way, on a
// log scale, each picture, from where it was set), white balance 2800..6500
// (automatic, settling at 4600), brightness (manual only). Its picture: as
// bright as its exposure (on a log scale), as warm as its white balance is low.
class StandIn : public JPCaptureSource {
public:
    bool   exposureAuto = false, balanceAuto = false;
    double exposure = 4900, balance = 2800;   // as set by hand: what it reads back (far off: overexposed)
    mutable double autoExposure = 0;          // where its automatic exposure has got to
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
        if (c["exposure"]["auto"].isBool()) {
            if (c["exposure"]["auto"].boolean() && !exposureAuto) autoExposure = exposure;
            exposureAuto = c["exposure"]["auto"].boolean();
        }
        if (!exposureAuto && c["exposure"]["value"].isNumber()) exposure = c["exposure"]["value"].number();
        if (c["white-balance"]["auto"].isBool()) balanceAuto = c["white-balance"]["auto"].boolean();
        if (!balanceAuto && c["white-balance"]["value"].isNumber()) balance = c["white-balance"]["value"].number();
        if (c["brightness"]["value"].isNumber()) brightness = int(c["brightness"]["value"].number());
    }
    // Its picture now: grey of a brightness from the exposure in use, tinted by the white balance in use.
    JPFrame picture() const {
        if (exposureAuto) autoExposure *= std::pow(333 / autoExposure, 0.05);
        const double e = exposureAuto ? autoExposure : exposure, w = balanceAuto ? 4600 : balance;
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

// Takes no exposure set by hand: set by hand, it is at its least whatever is asked.
class Stuck : public StandIn {
public:
    void setControls(const JJson& c) override {
        StandIn::setControls(c);
        if (!exposureAuto) exposure = 1;
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
    assert(!tune.step(cam, t + milliseconds(5000)));   // done once
    assert(tune.reached());
    // Values that do not give the picture aimed for: done, but not reached.
    {
        Stuck stuck;
        JPAutoTune st(1200, 200);
        assert(st.start(stuck, t));
        std::optional<JJson> got;
        for (int i = 0; i < 400 && !got; ++i) {
            t += milliseconds(33);
            st.see(stuck.picture(), t);
            got = st.step(stuck, t);
        }
        assert(got && !st.reached());
    }
    // No settings of its own: nothing to tune.
    Bare bare;
    JPAutoTune none(1200, 200);
    assert(!none.start(bare, t) && bare.sets == 0);
    return 0;
}
