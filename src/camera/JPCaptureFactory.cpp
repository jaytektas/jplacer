// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPCaptureFactory.h"
#include "JPImageSource.h"
#include "JPMjpgSource.h"
#include "JPOnvifSource.h"

#include "JPSimulatedSource.h"
#if defined(__linux__)
#include "JPV4L2Source.h"
#endif

#include <j/config/Json.h>

inline namespace jf {

std::unique_ptr<JPCaptureSource> JPCaptureFactory::create(const std::string& cameraName, const JJson& device,
                                                          std::string& error,
                                                          std::function<bool(double&, double&)> view,
                                                          const JPSwitcherSource::Links* links, std::function<bool()> takeClaim) {
    const std::string& backend = device["backend"].str();
    if (backend == "simulated")
        return std::make_unique<JPSimulatedSource>(cameraName, int(device["width"].number()),
                                                   int(device["height"].number()), device["fps"].number(),
                                                   device["scene"], std::move(view),
                                                   int(device["hangAfterFrames"].number()),
                                                   int(device["freezeAfterFrames"].number()));
    if (backend == "mjpg") {
        // OpenPnP's MjpgCaptureCamera.
        return std::make_unique<JPMjpgSource>(cameraName, device["url"].str(), int(device["width"].number(960)),
                                              int(device["height"].number(720)), int(device["timeoutMs"].number(3000)));
    }
    if (backend == "onvif") {
        // OpenPnP's OnvifIPCamera.
        JPOnvifSource::Settings s;
        s.host = device["host"].str();
        s.username = device["username"].str();
        s.password = device["password"].str();
        s.preferredResolution = device["preferredResolution"].str();
        s.resizeWidth = int(device["resizeWidth"].number(0));
        s.resizeHeight = int(device["resizeHeight"].number(0));
        s.fps = device["fps"].number(10);
        return std::make_unique<JPOnvifSource>(cameraName, s);
    }
    if (backend == "switcher") {
        // OpenPnP's SwitcherCamera.
        JPSwitcherSource::Settings s;
        s.cameraId = device["camera"].str();
        s.switcher = int(device["switcher"].number(0));
        s.actuatorId = device["actuator"].str();
        s.actuatorValue = device["actuatorValue"].number(0);
        s.delayMs = int(device["actuatorDelayMs"].number(500));
        return std::make_unique<JPSwitcherSource>(cameraName, s, links ? *links : JPSwitcherSource::Links {}, std::move(takeClaim));
    }
    if (backend == "image") {
        // OpenPnP's ImageCamera.
        JPImageSource::Settings s;
        s.path = device["source"].str();
        s.width = int(device["width"].number(640));
        s.height = int(device["height"].number(480));
        s.fps = device["fps"].number(10);
        s.unitsPerPixelX = device["imageUnitsPerPixel"]["x"].number(0.04);
        s.unitsPerPixelY = device["imageUnitsPerPixel"]["y"].number(0.04);
        s.offsetX = device["imageOffset"]["x"].number(0);
        s.offsetY = device["imageOffset"]["y"].number(0);
        s.rotation = device["simulatedRotation"].number(0);
        s.scale = device["simulatedScale"].number(1);
        s.flipped = device["simulatedFlipped"].boolean();
        return std::make_unique<JPImageSource>(cameraName, s, std::move(view));
    }
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
