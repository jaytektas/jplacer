// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPNeoden4CameraLibrary.h"

#include "common/JPlacerLog.h"

#include <j/core/Log.h>

#include <chrono>
#include <thread>

#if defined(_WIN32)
#include <windows.h>
#else
#include <dlfcn.h>
#endif

inline namespace jf {

namespace {

void pause(int ms) {
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

// OpenPnP's pauses around the library's calls.
constexpr int kCallPauseMs = 10, kResetPauseMs = 100;

// The library loaded, a function of it found, it let go: the system's way.
#if defined(_WIN32)
void* openLibrary(const char* file, std::string& why) {
    if (const HMODULE h = LoadLibraryA(file)) return reinterpret_cast<void*>(h);
    why = "Windows error " + std::to_string(GetLastError());
    return nullptr;
}
void* symbol(void* h, const char* name) { return reinterpret_cast<void*>(GetProcAddress(reinterpret_cast<HMODULE>(h), name)); }
void closeLibrary(void* h) { FreeLibrary(reinterpret_cast<HMODULE>(h)); }
#else
void* openLibrary(const char* file, std::string& why) {
    if (void* h = ::dlopen(file, RTLD_NOW | RTLD_LOCAL)) return h;
    why = ::dlerror();
    return nullptr;
}
void* symbol(void* h, const char* name) { return ::dlsym(h, name); }
void closeLibrary(void* h) { ::dlclose(h); }
#endif

} // namespace

JPNeoden4CameraLibrary* JPNeoden4CameraLibrary::instance(std::string& error) {
    static JPNeoden4CameraLibrary library;
    static std::mutex loading;
    std::lock_guard lk(loading);
    if (!library.m_handle && !library.load(error)) return nullptr;
    return &library;
}

bool JPNeoden4CameraLibrary::load(std::string& error) {
    std::string why;
    void* h = openLibrary(kFile, why);
    if (!h) {
        error = std::string("the NeoDen camera library (") + kFile + ") could not be loaded: " + why;
        return false;
    }
    auto fn = [&](const char* name) { return symbol(h, name); };
    m_init = reinterpret_cast<Init>(fn("img_init"));
    m_readAsy = reinterpret_cast<ReadAsy>(fn("img_readAsy"));
    m_reset = reinterpret_cast<Reset>(fn("img_reset"));
    m_setExposure = reinterpret_cast<SetShort>(fn("img_set_exp"));
    m_setGain = reinterpret_cast<SetShort>(fn("img_set_gain"));
    m_setLeftTop = reinterpret_cast<SetPair>(fn("img_set_lt"));
    m_setSize = reinterpret_cast<SetPair>(fn("img_set_wh"));
    if (!m_init || !m_readAsy || !m_reset || !m_setExposure || !m_setGain || !m_setLeftTop || !m_setSize) {
        error = std::string(kFile) + " is not the NeoDen camera library (a function is missing)";
        closeLibrary(h);
        return false;
    }
    m_handle = h;
    // OpenPnP's initializeCameras.
    std::lock_guard lk(m_mutex);
    const int cameras = m_init();
    if (cameras < 2) JLOGC(JPlacerLog::kCamera, JLogLevel::Error) << "NeoDen cameras: detected " << cameras;
    else JLOGC(JPlacerLog::kCamera, JLogLevel::Info) << "NeoDen cameras: detected " << cameras;
    pause(kResetPauseMs);
    for (const int c : { kDownCamera, kUpCamera }) {
        m_reset(c);
        pause(kCallPauseMs);
        m_setSize(c, int16_t(kInitialSize), int16_t(kInitialSize));
        pause(kCallPauseMs);
        m_setLeftTop(c, 0, 0);
        pause(kCallPauseMs);
    }
    pause(kResetPauseMs - kCallPauseMs);
    return true;
}

bool JPNeoden4CameraLibrary::read(int camera, int width, int height, int timeoutMs, std::vector<uint8_t>& grey, std::string& error) {
    std::lock_guard lk(m_mutex);
    grey.assign(size_t(width) * size_t(height), 0);
    pause(kCallPauseMs);
    const int ret = m_readAsy(camera, grey.data(), int(grey.size()), timeoutMs);
    if (ret == 1) return true;
    error = "img_readAsy() ret = " + std::to_string(ret) + ", [cameraId:" + std::to_string(camera) + "]";
    resetLocked(camera);
    return false;
}

void JPNeoden4CameraLibrary::reset(int camera) {
    std::lock_guard lk(m_mutex);
    resetLocked(camera);
}

void JPNeoden4CameraLibrary::resetLocked(int camera) {
    pause(kResetPauseMs);
    pause(kCallPauseMs);
    m_reset(camera);
    pause(kCallPauseMs);
    m_setExposure(camera, int16_t(m_exposure));
    pause(kCallPauseMs);
    m_setGain(camera, int16_t(m_gain));
    pause(kResetPauseMs);
}

void JPNeoden4CameraLibrary::exposureAndGain(int camera, int exposure, int gain) {
    std::lock_guard lk(m_mutex);
    if (exposure == m_exposure && gain == m_gain) return;
    m_exposure = exposure;
    m_gain = gain;
    resetLocked(camera);
}

void JPNeoden4CameraLibrary::window(int camera, int left, int top, int width, int height) {
    std::lock_guard lk(m_mutex);
    m_setSize(camera, int16_t(width), int16_t(height));
    pause(kCallPauseMs);
    m_setLeftTop(camera, int16_t(left), int16_t(top));
    pause(kCallPauseMs);
}

} // inline namespace jf
