// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPSimulatedSource.h"
#include "machine/JPMachineLocation.h"

#include <j/config/Json.h>

#include <array>
#include <optional>
#include <string>
#include <vector>

inline namespace jf {

// OpenPnP's SimulatedUpCamera: a simulated camera looking up at the nozzles
// (JPSimulatedSource, its picture; the machine adds the nozzle tips and the
// parts on them). Its settings, as its device's ("openpnpClass":
// "SimulatedUpCamera"): where it physically is (Camera Location; none, where
// it is set up to be), its picture's size and Simulated Units per Pixel, a
// lens of Focal Length over a sensor Sensor Diagonal across (things higher
// or lower than its focus seen smaller or bigger, and darker or brighter),
// Pick Error Offsets (a part picked that far off), View mirrored?, Simulate
// Focal Blur? and a Background Scenario (the shade behind and the nozzle
// tip's colour). A camera looking up sees the machine mirrored: X runs the
// other way to a camera looking down (View mirrored? turns that back).
class JPSimulatedUpCamera {
public:
    // OpenPnP's BackgroundScenario: its name, the background's shade and the nozzle tip's colour (RGB).
    struct Scenario {
        const char*            name;
        std::array<uint8_t, 3> shade, nozzleTip;
    };
    static const std::vector<Scenario>& scenarios();
    static const Scenario& scenario(const std::string& name);   // Dark when not one
    static constexpr const char* kDefaultScenario = "Dark";
    static constexpr double kDefaultUnitsPerPixel = 0.0234375;   // OpenPnP's
    static constexpr double kDefaultFocalLengthMm = 6, kDefaultSensorDiagonalMm = 4.4;
    static constexpr int    kDefaultWidth = 640, kDefaultHeight = 480;
    static constexpr double kFps = 10;
    // Beyond twice its distance from the lens, nothing is drawn (OpenPnP's frustum).
    static constexpr double kFarthest = 2;
    // OpenPnP's focal blur: a blur radius of this many pixels a millimetre out of focus, per mm a pixel; at most kMostBlurPx.
    static constexpr double kBokehMm = 0.01, kMostBlurPx = 5;
    static constexpr float  kNoise = 2;

    struct Settings {
        int    width = kDefaultWidth, height = kDefaultHeight;
        double uppX = kDefaultUnitsPerPixel, uppY = kDefaultUnitsPerPixel;
        bool   mirrored = false;
        bool   focalBlur = false;
        std::optional<JPMachineLocation> location;
        double focalLengthMm = kDefaultFocalLengthMm, sensorDiagonalMm = kDefaultSensorDiagonalMm;
        JPMachineLocation errorOffsets;
        std::string scenario = kDefaultScenario;

        // From its device's settings (a cell from before they were kept: from its scene's scale).
        static Settings fromDevice(const JJson& device);
        // How far the lens is from what is in focus (mm), for the view it has.
        double cameraDistance() const;
        // How much smaller something `dz` mm above the focus is seen (1: in focus); its shade divided by its square.
        double perspective(double dz) const { return (cameraDistance() + dz) / cameraDistance(); }
        // The blur of something `dz` mm from the focus, in pixels (0 without Simulate Focal Blur?).
        double blurPx(double dz) const;
        // The picture's scene (JPSimulatedSource): its scale, turn and mirroring, and its shade.
        JJson scene() const;
    };
    static bool is(const JJson& device) { return device["openpnpClass"].str() == "SimulatedUpCamera"; }

    // A nozzle over it: its axis at (x, y), its tip at `tipZ` and so wide (mm), turned `angle` (the part's way).
    struct Nozzle {
        double x = 0, y = 0, tipZ = 0, tipDiameter = 0, angle = 0;
    };
    // A part on a nozzle: its body's and pads' outlines in its own frame (mm), and its height (0: unknown).
    using Polygon = std::vector<std::pair<double, double>>;
    struct Part {
        Polygon              body;
        std::vector<Polygon> pads;
        double               heightMm = 0;
    };
    // The nozzle tip and the part on it (none: no part) as this camera at (cameraX, cameraY, cameraZ) sees them from
    // below, added to `extras`. As OpenPnP's SimulatedUpCamera draws them (`openPnp`: its settings): seen in
    // perspective, shaded and blurred as far from the focus as they are, the tip in its scenario's colour, the body
    // dark grey and the pads white, the part off by the Pick Error Offsets and turned as the nozzle is. Without
    // (a jplacer simulated camera looking up): flat, in greys.
    static void drawNozzle(const Settings* openPnp, double cameraX, double cameraY, double cameraZ, const Nozzle& nozzle,
                           const Part* part, JPSimulatedSource::Extras& extras);
    // A tip and a part's body and pads, in greys (a jplacer camera) and in colour (OpenPnP's).
    static constexpr float kTipLevel = 230, kBodyLevel = 60, kPadLevel = 255;
    static constexpr std::array<float, 3> kBodyColor { 60, 60, 60 }, kPadColor { 255, 255, 255 };
    // A part of no known height is drawn as this high (mm).
    static constexpr double kPartHeightMm = 1;
};

} // inline namespace jf
