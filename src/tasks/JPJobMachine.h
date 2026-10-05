// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "model/JPLocation.h"
#include "pipeline/JPPipeline.h"
#include "vision/JPPartFinder.h"
#include "vision/JPTemplateFinder.h"

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

inline namespace jf {

// What a job (JPJobProcessor) asks of the machine, each call made on the
// job's own thread and waiting until it is done: false, with why, when it
// could not be. Places are in the machine's coordinates, a nozzle's place
// being where its tip touches (the part's angle as its rotation); a camera's
// is where it looks.
class JPJobMachine {
public:
    struct Nozzle {
        std::string              id, name;
        std::string              tipId;    // on it now; empty: none
        std::vector<std::string> tipIds;   // the tips that fit it
    };

    virtual ~JPJobMachine() = default;

    // The head's nozzles, in order, and the machine's nozzle tips (id, name).
    virtual std::vector<Nozzle> nozzles() const = 0;
    virtual std::vector<std::pair<std::string, std::string>> tips() const = 0;
    // Where the head's camera is now; none when it cannot be told.
    virtual std::optional<JPLocation> cameraLocation() const = 0;

    // Whether the machine has been homed (until then nothing moves).
    virtual bool isHomed() const = 0;
    // Every nozzle up into its safe zone.
    virtual bool safeZ(std::string& why) = 0;
    // The tip on `nozzleId` changed for `tipId`: the one on it unloaded, then `tipId` loaded.
    virtual bool changeTip(const std::string& nozzleId, const std::string& tipId, std::string& why) = 0;
    // Turn the nozzle to `angle` where it is (OpenPnP's pre-rotation); a nozzle without a rotation axis stays.
    virtual bool rotate(const std::string& nozzleId, double angle, std::string& why) = 0;
    // Up to safe Z, across and turned to `at`, down to its Z, the part picked
    // (the vacuum on, the dwell, the part checked as the tip says), and up.
    virtual bool pick(const std::string& nozzleId, const JPLocation& at, std::string& why) = 0;
    // The same to `at`, the part let go there (and checked gone).
    virtual bool place(const std::string& nozzleId, const JPLocation& at, std::string& why) = 0;
    // What the nozzle holds dropped at the discard location.
    virtual bool discard(const std::string& nozzleId, std::string& why) = 0;
    // What a nozzle holds now (OpenPnP's Nozzle.getPart): the part picked, or "" once placed or discarded.
    virtual void holding(const std::string& /*nozzleId*/, const std::string& /*partId*/) {}
    // The nozzle over `at` at safe Z, turned to its rotation (not down).
    virtual bool positionNozzle(const std::string& nozzleId, const JPLocation& at, std::string& why) = 0;
    // The head camera over `at` (as it is, its height kept).
    virtual bool positionCamera(const JPLocation& at, std::string& why) = 0;
    // An actuator, named as OpenPnP names it (on the head, else on the
    // machine), actuated with `value`: a switch on when it is not 0, a
    // number or text set to it.
    virtual bool actuate(const std::string& actuatorName, double value, std::string& why) = 0;
    // The same with a text value (OpenPnP's actuate(String): a Rapid
    // feeder's "address pitch").
    virtual bool actuateText(const std::string& actuatorName, const std::string& value, std::string& why) = 0;
    // An actuator (named as for actuate) read, with `parameter` as its read
    // command's value (OpenPnP's actuator.read(parameter): a Schultz feeder's
    // number, a Photon packet): `value`.
    virtual bool readActuator(const std::string& actuatorName, const std::string& parameter, std::string& value,
                              std::string& why) = 0;
    // An actuator on the head (named as for actuate) straight to `at` from
    // where it is, not up to safe Z first (OpenPnP's actuator.moveTo); its Z
    // too when `withZ`; at `speed` (of the machine's).
    virtual bool moveActuator(const std::string& actuatorName, const JPLocation& at, bool withZ, double speed,
                              std::string& why) = 0;
    // OpenPnP's drag feeder vision: the head's camera over `at` (at safe Z),
    // the template image in the PNG file `templatePath` found within `area`
    // of its picture, and how far `at` is from where the template's middle
    // is: `offset` (X, Y; at less where it is).
    virtual bool matchTemplate(const JPLocation& at, const std::string& templatePath, const JPTemplateFinder::Area& area,
                               JPLocation& offset, std::string& why) = 0;
    // The head to its park place.
    virtual bool park(std::string& why) = 0;
    // How a fiducial is looked at (its Fiducial Vision Settings): up to so
    // many passes, done when one moves it less than the max linear offset;
    // with a parallax diameter, from either side of it.
    struct FiducialLook {
        int    passes = 3;
        double maxLinearOffsetMm = 0.2;
        double parallaxDiameterMm = 0;
        double parallaxAngle = 0;
        // OpenPnP's averaging: the passes after the first averaged.
        bool   averaging = false;
        // Found by this OpenPnP pipeline (prepared for the fiducial's part,
        // `partId`), else by jplacer's finder.
        std::shared_ptr<JPPipeline> pipeline;
        std::string                 partId;
    };
    // The camera over `nominal` (at safe Z), a round mark of `diameterMm`
    // found near there (looked at again, centred, as `look` says), and where
    // it is: `found`.
    virtual bool locateFiducial(const JPLocation& nominal, double diameterMm, const FiducialLook& look, JPLocation& found,
                                std::string& why) = 0;
    // A strip's sprocket hole: the camera over `nominal` (with a parallax
    // diameter, from either side of it, `parallaxAngle` turned, the two
    // finds averaged), a round mark of `diameterMm` found within `searchMm`
    // of it, nearest first: `found`. One look from each place.
    // Bottom vision (OpenPnP's part alignment): the part on the nozzle over
    // the camera looking up, as high as the part (its bottom where the
    // camera is focused), turned to `imageAngle`; found by its shape (its
    // pads, else its body) within `angleRange` either way of the angle it
    // should have there; with more passes, the nozzle moved and turned to
    // put it where it should be and looked at again, until it moves less
    // than `maxLinearOffsetMm` and turns less than a tenth of a degree.
    struct AlignRequest {
        std::vector<JPPartFinder::Rect> shape;   // the part's own mm
        double partHeightMm = 0;
        double imageAngle = 0;    // the nozzle's turn for the first look
        double angleRange = 10;
        int    passes = 3;
        double maxLinearOffsetMm = 1;
        // Found by this OpenPnP pipeline (the part's bottom vision settings'),
        // prepared for the part and its settings, else by jplacer's finder.
        std::shared_ptr<JPPipeline> pipeline;
        std::string                 partId, settingsId;
    };
    // Where the part is on the nozzle as last looked at: the nozzle's turn
    // then, the part's centre less the nozzle's axis (mm), and the part's angle.
    struct AlignResult {
        double nozzleAngle = 0;
        double dx = 0, dy = 0;
        double partAngle = 0;
        double cameraX = 0, cameraY = 0;   // where the camera looking up is
    };
    // The head camera over `at` (its X and Y), `pipeline` run on its picture
    // and shown on the camera for `showMs`: its "results" stage's
    // rectangles, each where it is on the machine and its angle as OpenPnP
    // reads it from the picture (Y down, clockwise positive), and how far
    // the camera sees either way of its centre. False (and why) when the
    // pipeline fails or has no rectangles stage; none found is no failure.
    struct SeenRects {
        struct Rect {
            double x = 0, y = 0, pixelAngle = 0;
        };
        std::vector<Rect> rects;
        double            halfWidthMm = 0, halfHeightMm = 0;
    };
    virtual bool seeRects(const JPLocation& at, JPPipeline& pipeline, int showMs, SeenRects& seen, std::string& why) = 0;
    // As seeRects, its "results" stage's circles (pixels, as found), the
    // camera's centre's pixel and scale, and a pixel's place on the machine.
    struct SeenCircles {
        struct Circle {
            double x = 0, y = 0, diameter = 0;
        };
        std::vector<Circle> circles;
        double              centreX = 0, centreY = 0, pixelsPerMm = 0;
        std::function<bool(double px, double py, double& x, double& y)> toMachine;
    };
    virtual bool seeCircles(const JPLocation& at, JPPipeline& pipeline, SeenCircles& seen, std::string& why) = 0;
    // The head camera over `at`, `pipeline` run on its picture there (each
    // stage's result left in it): where it looked from, its scale (mm per
    // pixel) and picture size, a pixel's place on the machine and back, and
    // whether it was calibrated at two heights (its scale then depends on Z).
    struct Sight {
        JPLocation at { JPLengthUnit::Millimeters };
        double     mmPerPixelX = 0, mmPerPixelY = 0;
        int        width = 0, height = 0;
        bool       twoHeights = false;
        std::function<JPLocation(double px, double py)>                   toMachine;
        std::function<bool(const JPLocation& l, double& px, double& py)> toPixel;
    };
    virtual bool lookThrough(const JPLocation& at, JPPipeline& pipeline, Sight& sight, std::string& why) = 0;
    // The head camera's scale, picture size and calibration (a Sight but for where it looks), not moving it.
    virtual bool cameraSight(Sight& sight, std::string& why) = 0;
    // A picture (BGR, as a pipeline's) on the head camera's view for `ms`.
    virtual void showOnCamera(const cv::Mat& bgr, int ms) = 0;
    virtual bool alignPart(const std::string& nozzleId, const AlignRequest& request, AlignResult& result,
                           std::string& why) = 0;
    virtual bool locateHole(const JPLocation& nominal, double diameterMm, double searchMm, double parallaxDiameterMm,
                            double parallaxAngle, JPLocation& found, std::string& why) = 0;
};

} // inline namespace jf
