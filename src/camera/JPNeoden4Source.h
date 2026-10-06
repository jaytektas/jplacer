// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPCaptureSource.h"

#include <optional>
#include <string>
#include <vector>

inline namespace jf {

// A NeoDen 4 camera through the machine's camera library
// (JPNeoden4CameraLibrary): OpenPnP's Neoden4Camera (its Camera Id, picture
// Width x Height at Shift X, Shift Y on the sensor, and Timeout), or a
// Neoden4SwitcherCamera (its Switcher Number the camera read, its Exposure
// and Gain set, in the source Neoden4Camera's size and timeout). Grey
// pictures, given as RGBA; a failed read resets the camera.
class JPNeoden4Source : public JPCaptureSource {
public:
    struct Settings {
        int camera = 1;
        int width = 1024, height = 1024;
        int timeoutMs = 1000;
        int shiftX = 0, shiftY = 0;
        // A switcher camera's: set before each picture (a change resets the camera).
        std::optional<int> exposure, gain;
    };

    JPNeoden4Source(std::string cameraName, Settings settings);

    bool open(std::string& error) override;
    void close() override {}
    std::vector<JPCaptureMode> modes() const override;
    bool start(const JPCaptureMode&, std::string&) override { return true; }
    bool grab(JPFrame& frame, int timeoutMs, std::string& error) override;
    std::string describe() const override;

private:
    std::string          m_name;
    Settings             m_settings;
    std::vector<uint8_t> m_grey;
    bool                 m_open = false;
};

} // inline namespace jf
