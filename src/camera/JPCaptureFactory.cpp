// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPCaptureFactory.h"

#include "JPSimulatedSource.h"
#if defined(__linux__)
#include "JPV4L2Source.h"
#endif

#include <j/config/Json.h>

inline namespace jf {

std::unique_ptr<JPCaptureSource> JPCaptureFactory::create(const std::string& cameraName, const JJson& device,
                                                          std::string& error,
                                                          std::function<bool(double&, double&)> view) {
    const std::string& backend = device["backend"].str();
    if (backend == "simulated")
        return std::make_unique<JPSimulatedSource>(cameraName, int(device["width"].number()),
                                                   int(device["height"].number()), device["fps"].number(),
                                                   device["scene"], std::move(view),
                                                   int(device["hangAfterFrames"].number()),
                                                   int(device["freezeAfterFrames"].number()));
#if defined(__linux__)
    if (backend == "v4l2") {
        if (device["name"].str().empty()) {
            error = cameraName + ": no capture device named in its configuration";
            return nullptr;
        }
        return std::make_unique<JPV4L2Source>(device["name"].str(), device["controls"]);
    }
#endif
    error = cameraName + ": no capture backend '" + backend + "' on this system";
    return nullptr;
}

std::optional<JPCaptureMode> JPCaptureFactory::choose(const std::vector<JPCaptureMode>& modes, const JJson& device) {
    if (modes.empty()) return std::nullopt;
    const std::string& format = device["format"].str();
    const int w = int(device["width"].number()), h = int(device["height"].number());
    for (const JPCaptureMode& m : modes)
        if ((format.empty() || m.format == format) && m.width == w && m.height == h) {
            JPCaptureMode chosen = m;
            if (device["fps"].number() > 0) chosen.fps = device["fps"].number();
            return chosen;
        }
    auto rank = [](const JPCaptureMode& m) { return m.format == "MJPG" ? 1 : 0; };
    const JPCaptureMode* best = &modes.front();
    for (const JPCaptureMode& m : modes)
        if (rank(m) > rank(*best) || (rank(m) == rank(*best) && m.width * m.height > best->width * best->height))
            best = &m;
    return *best;
}

} // inline namespace jf
