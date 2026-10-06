// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPSimulatedUpCamera.h"

#include <algorithm>
#include <cmath>

inline namespace jf {

namespace {
constexpr double kPi = 3.14159265358979323846;
}

const std::vector<JPSimulatedUpCamera::Scenario>& JPSimulatedUpCamera::scenarios() {
    // OpenPnP's, in its order: the shade, then the nozzle tip's colour.
    static const std::vector<Scenario> k {
        { "Black", { 0x00, 0x00, 0x00 }, { 0x00, 0xFF, 0x00 } },
        { "Dark", { 0x22, 0x22, 0x22 }, { 0x00, 0xDD, 0x00 } },
        { "Green", { 0x22, 0xAA, 0x22 }, { 0x00, 0xDD, 0x00 } },
        { "Magenta", { 0xAA, 0x22, 0xAA }, { 0x44, 0x44, 0x44 } },
        { "BlackAndDark", { 0x00, 0x00, 0x00 }, { 0x44, 0x44, 0x44 } },
        { "DarkAndGrey", { 0x22, 0x22, 0x22 }, { 0x66, 0x66, 0x66 } },
        { "DarkAndBlueTint", { 0x22, 0x22, 0x22 }, { 0x44, 0x44, 0xAA } },
    };
    return k;
}

const JPSimulatedUpCamera::Scenario& JPSimulatedUpCamera::scenario(const std::string& name) {
    for (const Scenario& s : scenarios())
        if (name == s.name) return s;
    return scenario(kDefaultScenario);
}

JPSimulatedUpCamera::Settings JPSimulatedUpCamera::Settings::fromDevice(const JJson& device) {
    Settings s;
    s.width = int(device["width"].number(kDefaultWidth));
    s.height = int(device["height"].number(kDefaultHeight));
    s.uppX = device["simulatedUnitsPerPixel"]["x"].number(kDefaultUnitsPerPixel);
    s.uppY = device["simulatedUnitsPerPixel"]["y"].number(s.uppX);
    s.mirrored = device["simulatedFlipped"].boolean();
    // A cell from before these were kept: its scene's scale, and mirroring, as they were.
    if (!device["simulatedUnitsPerPixel"].isObject() && device["scene"]["pxPerMm"].size() == 4) {
        const JJson& m = device["scene"]["pxPerMm"];
        if (m[size_t(0)].number() != 0) s.uppX = 1 / std::abs(m[size_t(0)].number());
        if (m[size_t(3)].number() != 0) s.uppY = 1 / std::abs(m[size_t(3)].number());
        s.mirrored = m[size_t(0)].number() < 0;
    }
    s.focalBlur = device["simulateFocalBlur"].boolean();
    s.location = JPMachineLocation::fromJson(device["simulatedLocation"]);
    s.focalLengthMm = device["focalLengthMm"].number(kDefaultFocalLengthMm);
    s.sensorDiagonalMm = device["sensorDiagonalMm"].number(kDefaultSensorDiagonalMm);
    s.errorOffsets = JPMachineLocation::fromJson(device["errorOffsets"]).value_or(JPMachineLocation {});
    if (!device["backgroundScenario"].str().empty()) s.scenario = device["backgroundScenario"].str();
    return s;
}

double JPSimulatedUpCamera::Settings::cameraDistance() const {
    // OpenPnP's: the lens as far from the focus as makes the view's diagonal the sensor's, through the focal length.
    const double viewDiagonal = std::hypot(uppX * width, uppY * height);
    return focalLengthMm * viewDiagonal / sensorDiagonalMm;
}

double JPSimulatedUpCamera::Settings::blurPx(double dz) const {
    if (!focalBlur) return 0;
    return std::min(std::abs(dz) * kBokehMm / uppX, kMostBlurPx);
}

JJson JPSimulatedUpCamera::Settings::scene() const {
    // Looking up, X mirrored (View mirrored? turns it back), Y up the picture; then turned as the camera is.
    const double m0x = (mirrored ? -1.0 : 1.0) / uppX, m0y = 1.0 / uppY;
    const double turn = -(location ? location->rotation : 0.0) * kPi / 180, c = std::cos(turn), s = std::sin(turn);
    JJson scene = JJson::object();
    scene["pxPerMm"] = JJson::array();
    for (double v : { c * m0x, -s * m0y, s * m0x, c * m0y }) scene["pxPerMm"].push(v);
    scene["marks"] = JJson::array();
    const Scenario& sc = JPSimulatedUpCamera::scenario(scenario);
    scene["ground"] = (sc.shade[0] + sc.shade[1] + sc.shade[2]) / 3.0;
    scene["groundColor"] = JJson::array();
    for (uint8_t v : sc.shade) scene["groundColor"].push(double(v));
    scene["noise"] = double(kNoise);
    return scene;
}

} // inline namespace jf
