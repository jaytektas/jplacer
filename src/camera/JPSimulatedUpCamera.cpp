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

void JPSimulatedUpCamera::drawNozzle(const Settings* up, double camX, double camY, double camZ, const Nozzle& n, const Part* part,
                                     JPSimulatedSource::Extras& e) {
    using Rgb = std::array<float, 3>;
    // A thing dz above the focus is seen nearer the middle, smaller, by the perspective, and its colour darker by
    // the perspective squared.
    auto shaded = [](const Rgb& rgb, double perspective) {
        Rgb out;
        for (size_t k = 0; k < 3; ++k) out[k] = float(std::min(255.0, rgb[k] / (perspective * perspective)));
        return out;
    };
    auto seen = [&](double x, double y, double perspective) {
        return std::pair { camX + (x - camX) / perspective, camY + (y - camY) / perspective };
    };
    JPSimulatedSource::Extras::Spot tip { n.x, n.y, n.tipDiameter, kTipLevel, std::nullopt, 0 };
    if (up) {
        const double perspective = up->perspective(n.tipZ - camZ);
        if (perspective <= 0 || perspective > kFarthest) return;
        const Scenario& sc = scenario(up->scenario);
        std::tie(tip.x, tip.y) = seen(n.x, n.y, perspective);
        tip.diameter = n.tipDiameter / perspective;
        tip.color = shaded({ float(sc.nozzleTip[0]), float(sc.nozzleTip[1]), float(sc.nozzleTip[2]) }, perspective);
        tip.blurPx = float(up->blurPx(n.tipZ - camZ));
    }
    e.spots.push_back(tip);
    if (!part) return;
    // Its underside a part's height below the tip.
    const double partZ = n.tipZ - (part->heightMm > 0 ? part->heightMm : kPartHeightMm);
    const double perspective = up ? up->perspective(partZ - camZ) : 1;
    if (perspective <= 0 || perspective > kFarthest) return;
    const float blur = up ? float(up->blurPx(partZ - camZ)) : 0;
    const double a = n.angle * M_PI / 180, ca = std::cos(a), sa = std::sin(a);
    const JPMachineLocation err = up ? up->errorOffsets : JPMachineLocation {};
    const double ea = err.rotation * M_PI / 180, cea = std::cos(ea), sea = std::sin(ea);
    auto placed = [&](const Polygon& o, float level, const Rgb& rgb) {
        JPSimulatedSource::Extras::Outline out { {}, level, std::nullopt, blur };
        if (up) out.color = shaded(rgb, perspective);
        for (const auto& [x, y] : o) {
            // In the part's frame: off by the error, turned by its rotation; then turned as the nozzle is.
            const double px = err.x + x * cea - y * sea, py = err.y + x * sea + y * cea;
            out.points.push_back(seen(n.x + px * ca - py * sa, n.y + px * sa + py * ca, perspective));
        }
        e.outlines.push_back(std::move(out));
    };
    placed(part->body, kBodyLevel, kBodyColor);
    for (const Polygon& o : part->pads) placed(o, kPadLevel, kPadColor);
}

} // inline namespace jf
