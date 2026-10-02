// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPCaptureSource.h"

#include <chrono>

inline namespace jf {

// A camera with nothing behind it: a test picture (a grid, a moving bar) at
// the size and rate asked for, so the camera view and everything downstream
// run with no hardware.
class JPSimulatedSource : public JPCaptureSource {
public:
    JPSimulatedSource(std::string name, int width, int height, double fps);

    bool open(std::string& error) override;
    void close() override {}
    std::vector<JPCaptureMode> modes() const override;
    bool start(const JPCaptureMode& mode, std::string& error) override;
    bool grab(JPFrame& frame, int timeoutMs, std::string& error) override;
    std::string describe() const override { return m_name + " (simulated)"; }

private:
    std::string m_name;
    JPCaptureMode m_mode;
    uint64_t m_sequence = 0;
    std::chrono::steady_clock::time_point m_next;
};

} // inline namespace jf
