// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPCaptureSource.h"

#include <j/config/Json.h>

#include <chrono>
#include <functional>
#include <random>
#include <vector>

inline namespace jf {

// A camera with nothing behind it, at the size and rate asked for, so the
// camera view and everything downstream run with no hardware.
//
// Without a scene it draws a test picture (a grid, a moving bar). With one it
// draws what a camera at the machine's current viewpoint would see: round
// marks at their places on the machine, through a camera transform only the
// configuration knows (pixels per mm, rotation, mirroring), with soft edges,
// uneven light and noise. Calibration and visual homing are proven on it:
// they must find what it hides.
//
//   "scene": { "pxPerMm": [-25.7, 0, 0, 25.6],          // row-major, as JPCameraCalibration
//              "lensK1": -0.1, "lensK2": 0.02,          // the lens's bending (JPLens), none if left out
//              "lensCentre": [672, 373],                // where it bends about (else the middle)
//              "marks": [ { "x": 137.137, "y": 179.265, "diameter": 1.85 },
//                         { "x": 150, "y": 179, "diameter": 1, "level": 5 } ],   // its own brightness (a hole)
//              "ground": 30, "mark": 190, "noise": 3 }
class JPSimulatedSource : public JPCaptureSource {
public:
    // Where the camera is looking (machine X, Y); false when unknown.
    using ViewProvider = std::function<bool(double&, double&)>;

    // `hangAfterFrames`: after so many pictures it sends no more and says
    // nothing (as a camera wedged by noise on its cable does), until opened
    // again; 0 never. `freezeAfterFrames`: after so many it sends the very
    // same picture over and over (the other way a camera wedges), until
    // opened again; 0 never.
    JPSimulatedSource(std::string name, int width, int height, double fps,
                      const JJson& scene = JJson(), ViewProvider view = nullptr, int hangAfterFrames = 0,
                      int freezeAfterFrames = 0);

    bool open(std::string& error) override;
    void close() override {}
    std::vector<JPCaptureMode> modes() const override;
    bool start(const JPCaptureMode& mode, std::string& error) override;
    bool grab(JPFrame& frame, int timeoutMs, std::string& error) override;
    std::string describe() const override { return m_name + " (simulated)"; }

private:
    void drawScene(JPFrame& frame);

    struct Mark { double x, y, diameter; float level; };

    std::string m_name;
    JPCaptureMode m_mode;
    bool m_hasScene = false;
    double m_pxPerMm[4] = {};
    std::vector<Mark> m_marks;
    float m_ground = 0, m_noise = 0;
    double m_lensK1 = 0, m_lensK2 = 0;
    double m_lensCentre[2] = {};
    bool m_lensCentreSet = false;
    ViewProvider m_view;
    std::mt19937 m_rng{ 1 };
    uint64_t m_sequence = 0;
    int m_hangAfterFrames = 0;
    int m_freezeAfterFrames = 0;
    std::chrono::steady_clock::time_point m_next;
};

} // inline namespace jf
