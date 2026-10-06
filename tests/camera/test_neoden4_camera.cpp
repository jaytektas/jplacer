// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// The NeoDen 4's cameras through its camera library (a stand-in here, loaded
// as the real one is): loaded once, its down and up cameras set up as
// OpenPnP's handler does; a Neoden4Camera's picture (grey, given as RGBA) in
// its size and window; a Neoden4SwitcherCamera's at its exposure and gain,
// taken from its source camera's size (a change of exposure or gain resetting
// the camera); a failed read resetting it.
// Tests check with assert(); a Release build must not compile it away.
#undef NDEBUG
#include <cassert>

#include "camera/JPCaptureFactory.h"

#include <j/config/Json.h>

#include <dlfcn.h>
#include <string>

using namespace jf;

namespace {

std::string calls() {
    void* h = ::dlopen("libneodencam.so", RTLD_NOW | RTLD_NOLOAD);
    assert(h);
    using Calls = const char* (*)();
    const std::string c = reinterpret_cast<Calls>(::dlsym(h, "fake_calls"))();
    ::dlclose(h);
    return c;
}

void failReads(int n) {
    void* h = ::dlopen("libneodencam.so", RTLD_NOW | RTLD_NOLOAD);
    reinterpret_cast<void (*)(int)>(::dlsym(h, "fake_fail_reads"))(n);
    ::dlclose(h);
}

bool has(const std::string& all, const std::string& call) {
    return all.find(call + ";") != std::string::npos;
}

} // namespace

int main() {
    JJson down = JJson::object();
    down["backend"] = "neoden4";
    down["cameraId"] = 1;
    down["width"] = 64;
    down["height"] = 48;
    down["timeoutMs"] = 300;
    down["shiftX"] = 8;
    std::string error;
    auto cam = JPCaptureFactory::create("Top", down, error);
    assert(cam && cam->open(error));
    const std::string opened = calls();
    // Loaded: counted, cameras 1 and 5 at 1024 x 1024 from 0, 0; then this one's window, and a reset.
    assert(opened.rfind("init;reset 1;wh 1 1024 1024;lt 1 0 0;reset 5;wh 5 1024 1024;lt 5 0 0;", 0) == 0);
    assert(has(opened, "wh 1 64 48") && has(opened, "lt 1 8 0") && has(opened, "reset 1;exp 1 0;gain 1 0"));
    const auto modes = cam->modes();
    assert(modes.size() == 1 && modes[0].width == 64 && modes[0].height == 48);
    JPFrame f;
    assert(cam->grab(f, 50, error));
    assert(f.width == 64 && f.height == 48 && f.rgba.size() == 64u * 48u * 4u);
    assert(f.rgba[0] == 1 && f.rgba[1] == 1 && f.rgba[2] == 1 && f.rgba[3] == 255);
    assert(has(calls(), "read 1 3072 300"));   // its own timeout

    // A switcher camera: the source's size, its own camera number, exposure and gain.
    JJson up = JJson::object();
    up["backend"] = "neoden4Switcher";
    up["camera"] = "TOP";
    up["switcher"] = 5;
    up["exposure"] = 30;
    up["gain"] = 4;
    JPCaptureFactory::Context ctx;
    ctx.links.deviceOf = [down](const std::string& id) { return id == "TOP" ? down : JJson::object(); };
    auto sw = JPCaptureFactory::create("Bottom", up, error, ctx);
    assert(sw && sw->open(error));
    assert(has(calls(), "reset 5;exp 5 30;gain 5 4"));
    assert(sw->grab(f, 50, error) && f.rgba[0] == 5 + 30 + 4);
    assert(!has(calls(), "reset 5"));   // the same exposure and gain: not reset again
    // The down camera's picture now: the device's exposure and gain as the switcher left them.
    assert(cam->grab(f, 50, error) && f.rgba[0] == 1 + 30 + 4);
    calls();

    // A failed read: no picture, the camera reset with the exposure and gain.
    failReads(1);
    assert(!sw->grab(f, 50, error) && error.find("img_readAsy() ret = 0") != std::string::npos);
    assert(has(calls(), "reset 5;exp 5 30;gain 5 4"));

    // A switcher whose source is not a NeoDen camera: refused.
    JPCaptureFactory::Context none;
    none.links.deviceOf = [](const std::string&) { return JJson::object(); };
    assert(!JPCaptureFactory::create("Bad", up, error, none) && error.find("Source Camera") != std::string::npos);
    return 0;
}
