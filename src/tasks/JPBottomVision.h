// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "model/JPLocation.h"

#include <opencv2/core.hpp>

#include <functional>
#include <optional>
#include <string>
#include <utility>

inline namespace jf {

class JPFootprint;
class JPVisionComposite;
struct JPCameraCalibration;
struct JPNozzleTipConfig;
class JPPipeline;
class JPVisionSettings;

// OpenPnP's ReferenceBottomVision.findOffsets: where a part sits on the nozzle, as the camera looking up sees it.
// Pre-rotated (the part's settings or the machine say so): the nozzle turned to the placement's angle and the part
// looked at again, the nozzle moved by what was seen, until it is close enough or the passes run out; the offsets
// then are where the nozzle had to go. Otherwise one look at angle 0. Either way less the part's Vision Offset
// (turned as the part is), and checked against the nozzle tip's Max. Pick Tolerance and the part's size.
class JPBottomVision {
public:
    struct Settings {
        std::string partId;
        bool        preRotate = false;
        double      wantedAngle = 0;          // pre-rotated: the placement's angle on the machine
        int         maxVisionPasses = 3;
        double      maxLinearOffsetMm = 1, maxAngularOffset = 10;
        bool        fullRotation = false;     // Rotation Full; else Adjust (within ±45°)
        JPLocation  visionOffset { JPLengthUnit::Millimeters };
        // The Part size check's nominal width and length (mm); none: no check.
        std::optional<std::pair<double, double>> partCheckSizeMm;
        int         checkSizeTolerancePercent = 20;
        // The nozzle tip's Max. Pick Tolerance (mm) and name; none: no tip, no check.
        std::optional<double> maxPickToleranceMm;
        std::string           tipName;
    };
    // What one look saw: the part's rectangle on the machine (mm), its angle the machine's (right-handed), its
    // size as measured (0: not measured).
    struct Seen {
        double x = 0, y = 0, angle = 0;
        double widthMm = 0, heightMm = 0;
    };
    // The nozzle put at `nozzle` (X, Y and its rotation; the camera's height for the part) and the part found,
    // expected at `expectedAngle`; `pass` counts from 0.
    using Look = std::function<bool(const JPLocation& nozzle, double expectedAngle, int pass, Seen& seen, std::string& why)>;
    // OpenPnP's PartAlignmentOffset: X, Y and rotation offsets, and whether the part was turned to its placement's angle.
    struct Offset {
        JPLocation location { JPLengthUnit::Millimeters };
        bool       preRotated = false;
    };

    // OpenPnP takes a found rectangle's angle within this of the one wanted (Rotation Adjust), its sides alike to it.
    static constexpr double kAdjustRange = 45;
    // A step along a found rectangle's angle (pixels), to turn it into the machine's angle.
    static constexpr double kAngleStepPx = 100;

    // For the camera looking up at (cameraX, cameraY).
    static bool findOffsets(const Settings& settings, double cameraX, double cameraY, const Look& look, Offset& offset,
                            std::string& why);
    // OpenPnP's getPartCheckSize: the footprint's body (BodySize) or its pads' extent (PadExtents), mm; none when
    // the check is Disabled. `addTolerance`: grown by the tolerance.
    static std::optional<std::pair<double, double>> partCheckSize(const JPVisionSettings& settings, const JPFootprint& footprint,
                                                                  bool addTolerance = false);
    // A rectangle found in the picture (pixels) on the machine, through the camera's calibration (which knows how
    // the camera is turned and mirrored): its centre, its angle taken nearest `angle` (within `range` either way:
    // a rectangle tells no side from another), and its size.
    static bool seenOf(const cv::RotatedRect& rect, const JPCameraCalibration& cal, double cameraX, double cameraY, double angle,
                       double range, Seen& seen, std::string& why);
    // The part found by its bottom vision pipeline (prepared for it) in the picture of the camera looking up at
    // (cameraX, cameraY), the nozzle's axis at (nozzleX, nozzleY): where it should be and its angle given to the
    // pipeline (OpenPnP's wanted location), its rectangle then put on the machine (seenOf).
    static bool findByPipeline(JPPipeline& pipeline, const std::string& partId, const JPCameraCalibration& cal, double cameraX,
                               double cameraY, double nozzleX, double nozzleY, double angle, double range, Seen& seen,
                               std::string& why);
    // OpenPnP's vision compositing: a part bigger than one look seen in the shots of `composite`, its corners put
    // together. For each shot, in the order travelled from where the nozzle is (`nozzleAt`), `moveTo` puts the
    // nozzle (meant to be at nozzleX, nozzleY over the camera) so the shot's middle is over the camera, and the
    // pipeline (prepared for the shot) is run (`shown`: what it saw). `angle`: the part's, as expected.
    struct Composite {
        JPPipeline&                pipeline;
        JPVisionComposite&         composite;
        const JPNozzleTipConfig*   tip;
        const JPCameraCalibration& cal;
        double                     cameraX, cameraY;
        std::string                partId;
        std::function<bool(double& x, double& y)>                       nozzleAt;
        std::function<bool(double x, double y, std::string& why)>       moveTo;
        std::function<void(JPPipeline&)>                                shown;
    };
    static bool seeComposite(const Composite& c, double nozzleX, double nozzleY, double angle, Seen& seen, std::string& why);
    // The pipeline run and its result: one rectangle, as OpenPnP's processPipelineAndGetResult insists.
    static bool resultRect(JPPipeline& pipeline, const std::string& partId, cv::RotatedRect& rect, std::string& why);

private:
    static bool offsetsCheck(const Settings& s, const JPLocation& offsets, std::string& why);
    static bool partSizeCheck(const Settings& s, const Seen& seen, std::string& why);
};

} // inline namespace jf
