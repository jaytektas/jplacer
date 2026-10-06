// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPCaptureFactory.h"
#include "JPImageSource.h"
#include "JPGstreamerSource.h"
#include "JPMjpgSource.h"
#include "JPNeoden4Source.h"
#include "JPOnvifSource.h"

#include "JPSimulatedSource.h"
#include "JPSimulatedUpCamera.h"
#if defined(__linux__)
#include "JPV4L2Source.h"
#endif

#include <j/config/Json.h>

inline namespace jf {

std::unique_ptr<JPCaptureSource> JPCaptureFactory::create(const std::string& cameraName, const JJson& device,
                                                          std::string& error, const Context& context) {
    const std::string& backend = device["backend"].str();
    if (backend == "simulated" && JPSimulatedUpCamera::is(device)) {
        // OpenPnP's SimulatedUpCamera: its picture made from its own settings.
        const JPSimulatedUpCamera::Settings up = JPSimulatedUpCamera::Settings::fromDevice(device);
        return std::make_unique<JPSimulatedSource>(cameraName, up.width, up.height, device["fps"].number(JPSimulatedUpCamera::kFps),
                                                   up.scene(), context.view, 0, 0, context.extras);
    }
    if (backend == "simulated")
        return std::make_unique<JPSimulatedSource>(cameraName, int(device["width"].number()),
                                                   int(device["height"].number()), device["fps"].number(),
                                                   device["scene"], context.view,
                                                   int(device["hangAfterFrames"].number()),
                                                   int(device["freezeAfterFrames"].number()), context.extras);
    if (backend == "mjpg") {
        // OpenPnP's MjpgCaptureCamera.
        return std::make_unique<JPMjpgSource>(cameraName, device["url"].str(), int(device["width"].number(960)),
                                              int(device["height"].number(720)), int(device["timeoutMs"].number(3000)));
    }
    if (backend == "gstreamer")   // OpenPnP's GstreamerCamera.
        return std::make_unique<JPGstreamerSource>(cameraName, device["pipeline"].str());
    if (backend == "onvif") {
        // OpenPnP's OnvifIPCamera.
        JPOnvifSource::Settings s;
        s.host = device["host"].str();
        s.username = device["username"].str();
        s.password = device["password"].str();
        s.preferredResolution = device["preferredResolution"].str();
        s.resizeWidth = int(device["resizeWidth"].number(0));
        s.resizeHeight = int(device["resizeHeight"].number(0));
        s.fps = device["fps"].number(10.0);
        return std::make_unique<JPOnvifSource>(cameraName, s);
    }
    if (backend == "switcher") {
        // OpenPnP's SwitcherCamera.
        JPSwitcherSource::Settings s;
        s.cameraId = device["camera"].str();
        s.switcher = int(device["switcher"].number(0));
        s.actuatorId = device["actuator"].str();
        s.actuatorValue = device["actuatorValue"].number(0.0);
        s.delayMs = int(device["actuatorDelayMs"].number(500));
        return std::make_unique<JPSwitcherSource>(cameraName, s, context.links, context.takeClaim);
    }
    if (backend == "neoden4" || backend == "neoden4Switcher") {
        // OpenPnP's Neoden4Camera; a Neoden4SwitcherCamera takes its source Neoden4Camera's picture size and timeout.
        const bool switcher = backend == "neoden4Switcher";
        JJson source = device;
        if (switcher) {
            source = context.links.deviceOf ? context.links.deviceOf(device["camera"].str()) : JJson::object();
            if (source["backend"].str() != "neoden4") {
                error = cameraName + ": its Source Camera is not a NeoDen 4 camera";
                return nullptr;
            }
        }
        JPNeoden4Source::Settings s;
        s.camera = switcher ? int(device["switcher"].number(0)) : int(source["cameraId"].number(1));
        s.width = int(source["width"].number(1024));
        s.height = int(source["height"].number(1024));
        s.timeoutMs = int(source["timeoutMs"].number(1000));
        s.shiftX = int(source["shiftX"].number(0));
        s.shiftY = int(source["shiftY"].number(0));
        if (switcher) {
            s.exposure = int(device["exposure"].number(25));
            s.gain = int(device["gain"].number(8));
        }
        return std::make_unique<JPNeoden4Source>(cameraName, s);
    }
    if (backend == "image") {
        // OpenPnP's ImageCamera.
        JPImageSource::Settings s;
        s.path = device["source"].str();
        s.width = int(device["width"].number(640));
        s.height = int(device["height"].number(480));
        s.fps = device["fps"].number(10.0);
        s.unitsPerPixelX = device["imageUnitsPerPixel"]["x"].number(JPCameraConfig::kDefaultImageUnitsPerPixel);
        s.unitsPerPixelY = device["imageUnitsPerPixel"]["y"].number(JPCameraConfig::kDefaultImageUnitsPerPixel);
        s.offsetX = device["imageOffset"]["x"].number(0.0);
        s.offsetY = device["imageOffset"]["y"].number(0.0);
        s.rotation = device["simulatedRotation"].number(0.0);
        s.scale = device["simulatedScale"].number(1.0);
        s.flipped = device["simulatedFlipped"].boolean();
        s.distortion = device["simulatedDistortion"].number(0.0);
        s.yRotation = device["simulatedYRotation"].number(0.0);
        s.focalLengthMm = device["focalLengthMm"].number(6.0);
        s.sensorDiagonalMm = device["sensorDiagonalMm"].number(4.4);
        for (const auto& [key, field] : { std::pair { "primaryFiducial", &s.primaryFiducial }, std::pair { "secondaryFiducial", &s.secondaryFiducial } })
            if (const JJson& f = device[key]; f.isObject())
                *field = JPImageSource::Settings::Fiducial { f["x"].number(0.0), f["y"].number(0.0), f["z"].number(0.0) };
        return std::make_unique<JPImageSource>(cameraName, s, context.view);
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
