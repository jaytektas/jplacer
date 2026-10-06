// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

inline namespace jf {

// The NeoDen 4's camera library (libneodencam.so, the machine's own), loaded
// when first wanted, as OpenPnP's Neoden4CameraHandler loads it: its cameras
// counted and the down and up ones (1 and 5) reset to 1024 x 1024 at 0, 0.
// One device behind them all: every call is made in turn, and the exposure
// and gain last set are the device's (OpenPnP's Neoden4Camera keeps them so).
class JPNeoden4CameraLibrary {
public:
    static constexpr const char* kFile = "libneodencam.so";
    static constexpr int kDownCamera = 1, kUpCamera = 5;
    static constexpr int kInitialSize = 1024;

    // The library, loaded and its cameras set up; null with `error` when it cannot be.
    static JPNeoden4CameraLibrary* instance(std::string& error);

    // One grey picture of `camera` (width x height bytes), within `timeoutMs`;
    // false with `error` (the camera reset, as OpenPnP's does, its exposure and gain set again).
    bool read(int camera, int width, int height, int timeoutMs, std::vector<uint8_t>& grey, std::string& error);
    // OpenPnP's resetCamera: the camera reset, the exposure and gain set again.
    void reset(int camera);
    // OpenPnP's setCameraExposureAndGain: either changed, the camera reset with them.
    void exposureAndGain(int camera, int exposure, int gain);
    // Where the picture starts on the sensor (OpenPnP's img_set_lt), and its size.
    void window(int camera, int left, int top, int width, int height);

private:
    JPNeoden4CameraLibrary() = default;
    bool load(std::string& error);
    void resetLocked(int camera);

    using Init = int (*)();
    using ReadAsy = int (*)(int, uint8_t*, int, int);
    using Reset = int (*)(int);
    using SetShort = int (*)(int, int16_t);
    using SetPair = int (*)(int, int16_t, int16_t);

    std::mutex m_mutex;
    void*      m_handle = nullptr;
    Init       m_init = nullptr;
    ReadAsy    m_readAsy = nullptr;
    Reset      m_reset = nullptr;
    SetShort   m_setExposure = nullptr, m_setGain = nullptr;
    SetPair    m_setLeftTop = nullptr, m_setSize = nullptr;
    int        m_exposure = 0, m_gain = 0;
};

} // inline namespace jf
