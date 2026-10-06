// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// A stand-in for the NeoDen 4's camera library (libneodencam.so), for the
// tests: each call recorded; a picture is every byte its camera's number
// plus the exposure and gain last set; a read made to fail on request.

#include <cstdint>
#include <cstring>
#include <string>

namespace {
std::string s_calls;
int         s_exposure = 0, s_gain = 0;
int         s_failReads = 0;

void note(const std::string& call) {
    s_calls += call + ";";
}
} // namespace

extern "C" {

int img_init() { note("init"); return 2; }
int img_capture(int camera) { note("capture " + std::to_string(camera)); return 1; }
int img_led(int camera, int16_t mode) { note("led " + std::to_string(camera) + " " + std::to_string(mode)); return 1; }
int img_read(int, uint8_t*, int, int) { return 0; }
int img_readAsy(int camera, uint8_t* buffer, int bytes, int timeoutMs) {
    note("read " + std::to_string(camera) + " " + std::to_string(bytes) + " " + std::to_string(timeoutMs));
    if (s_failReads > 0) {
        --s_failReads;
        return 0;
    }
    std::memset(buffer, camera + s_exposure + s_gain, size_t(bytes));
    return 1;
}
int img_reset(int camera) { note("reset " + std::to_string(camera)); return 1; }
int img_set_exp(int camera, int16_t exposure) {
    s_exposure = exposure;
    note("exp " + std::to_string(camera) + " " + std::to_string(exposure));
    return 1;
}
int img_set_gain(int camera, int16_t gain) {
    s_gain = gain;
    note("gain " + std::to_string(camera) + " " + std::to_string(gain));
    return 1;
}
int img_set_lt(int camera, int16_t left, int16_t top) {
    note("lt " + std::to_string(camera) + " " + std::to_string(left) + " " + std::to_string(top));
    return 1;
}
int img_set_wh(int camera, int16_t w, int16_t h) {
    note("wh " + std::to_string(camera) + " " + std::to_string(w) + " " + std::to_string(h));
    return 1;
}

// The stand-in's own: what was called (and forgotten), and reads to fail.
const char* fake_calls() {
    static std::string taken;
    taken = s_calls;
    s_calls.clear();
    return taken.c_str();
}
void fake_fail_reads(int n) { s_failReads = n; }

}
