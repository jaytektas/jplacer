// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "machine/JPNozzleConfig.h"
#include "model/JPLocation.h"
#include "pipeline/JPPipeline.h"
#include "tasks/JPBottomVision.h"
#include "tasks/JPTravel.h"
#include "pipeline/JPTemplateFinder.h"

#include <j/config/Json.h>

#include <array>
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
        // Waited after its vacuum is on (pick) or off (place): its own and its tip's.
        int                      pickDwellMs = 0, placeDwellMs = 0;
        // Its offset from the head (mm): a place it goes to, less this, is where the head goes (OpenPnP's
        // toHeadLocation).
        double                   offsetX = 0, offsetY = 0;
        // OpenPnP's Rotation Mode: how its turn relates to the part's angle
        // ("AbsolutePartAngle", "PlacementAngle", "MinimalRotation",
        // "LimitedArticulation"); for the last, how far it may turn either way
        // of the pick and of alignment, and its rotation axis's range.
        bool                     tipChangeOnManualPick = false;
        std::string              rotationMode = "AbsolutePartAngle";
        double                   maxPickArticulation = 15, maxAlignArticulation = 30;
        double                   rotationLow = -180, rotationHigh = 180;
        // OpenPnP's Align with Part (aligning rotation mode): bottom vision's
        // turn of the part taken into its rotation mode offset, so it reads
        // the part's angle as aligned.
        bool                     alignRotationWithPart = false;
        // OpenPnP's ContactProbeNozzle (JPNozzleConfig::ContactProbe), and the
        // tallest part its tip takes (a part of unknown height probed from there).
        JPNozzleConfig::ContactProbe contactProbe;
        double                   tipMaxPartHeightMm = 0;
    };

    virtual ~JPJobMachine() = default;

    // The head's nozzles, in order, and the machine's nozzle tips (id, name).
    virtual std::vector<Nozzle> nozzles() const = 0;
    virtual std::vector<std::pair<std::string, std::string>> tips() const = 0;
    // OpenPnP's rotation mode offset of a nozzle (JPCell::setRotationModeOffset):
    // its rotation the part's angle, its axis that much less; none: its axis's.
    virtual void setRotationModeOffset(const std::string& nozzleId, std::optional<double> offset) = 0;
    // How far a nozzle's rotation axis is turned now (no rotation mode offset);
    // none when it cannot be told.
    virtual std::optional<double> nozzleRotation(const std::string& nozzleId) const {
        (void)nozzleId;
        return std::nullopt;
    }
    // Where the head's camera is now; none when it cannot be told.
    virtual std::optional<JPLocation> cameraLocation() const = 0;
    // A fiducial check about to look for a fiducial of part `partId` (from its own thread): for the cameras
    // to show its footprint.
    virtual void lookingFor(const std::string& /*partId*/) {}
    // The head camera's looks from now on, until told again, for feeder `feederId`: at `controls` (its tune), or
    // with none, tuned on the first of them when `tuneFirst` (the tune then the feeder's, JPFeeder::cameraTune),
    // else at the camera's own settings; a fiducial check's tune still for its fiducials.
    virtual void useHeadTune(const std::string& /*feederId*/, const std::optional<JJson>& /*controls*/, bool /*tuneFirst*/) {}
    // OpenPnP's TravelCost for routes (JPTravel): the default head's camera's X, Y (and Z, where a controller's)
    // axes; none (the routes by straight-line distance, as OpenPnP's when it cannot make one) when not known.
    virtual std::optional<JPTravel::Cost> travelCost() const { return std::nullopt; }
    // Whether the head camera can be taken to `at` (its axes within their soft limits).
    virtual bool cameraReaches(const JPLocation& at) const = 0;
    // A nozzle tip's Push and Drag Usage: whether it may push, and its outside diameter at its lowest (mm).
    struct TipPush {
        bool   allowed = false;
        double diameterLowMm = 0;
    };
    virtual TipPush tipPush(const std::string& tipId) const = 0;
    // The part a nozzle holds (empty: none), and the nozzle chosen on the Jog panel (empty: none).
    virtual std::string holdingPart(const std::string& nozzleId) const = 0;
    virtual std::string chosenNozzle() const = 0;
    // An actuator on the head to `to` (X, Y, Z, rotation; one not given stays
    // as it is) at `speed` (0..1) of the machine's: up to safe Z and across
    // first when `safeZFirst`, else straight there, every axis at once.
    virtual bool positionActuator(const std::string& actuatorName, std::array<std::optional<double>, 4> to, double speed,
                                  bool safeZFirst, std::string& why) = 0;
    // An actuator's rotation axis called 0 where it is now (nothing moves; nothing when it has none).
    virtual bool zeroActuatorRotation(const std::string& actuatorName, std::string& why) = 0;

    // Whether the machine has been homed (until then nothing moves).
    virtual bool isHomed() const = 0;
    // Every nozzle up into its safe zone.
    virtual bool safeZ(std::string& why) = 0;
    // The tip on `nozzleId` changed for `tipId`: the one on it unloaded, then `tipId` loaded.
    virtual bool changeTip(const std::string& nozzleId, const std::string& tipId, std::string& why) = 0;
    // Turn the nozzle to `angle` with the next move made at safe Z (OpenPnP's pre-rotation, a subordinate move); a
    // nozzle without a rotation axis stays.
    virtual bool rotate(const std::string& nozzleId, double angle, std::string& why) = 0;
    // Up to safe Z, across and turned to `at`, down to its Z, the part picked
    // (the vacuum on, the dwell), and up.
    virtual bool pick(const std::string& nozzleId, const JPLocation& at, std::string& why) = 0;
    // The same to `at`, the part let go there.
    virtual bool place(const std::string& nozzleId, const JPLocation& at, std::string& why) = 0;
    // OpenPnP's vacuum part detection: whether the nozzle's tip checks at
    // `step` (Nozzle.isPartOnEnabled / isPartOffEnabled), and the check
    // (isPartOn / isPartOff, at safe Z): false, with why, when it could not
    // be made; else `on` / `off` as sensed. A machine that senses no vacuum checks nothing.
    enum class VacuumStep { AfterPick, Align, BeforePlace, AfterPlace, BeforePick };
    virtual bool vacuumChecked(const std::string& /*nozzleId*/, VacuumStep /*step*/) const { return false; }
    virtual bool partOn(const std::string& /*nozzleId*/, bool& on, std::string& /*why*/) {
        on = true;
        return true;
    }
    virtual bool partOff(const std::string& /*nozzleId*/, bool& off, std::string& /*why*/) {
        off = true;
        return true;
    }
    // What the nozzle holds dropped at the discard location.
    virtual bool discard(const std::string& nozzleId, std::string& why) = 0;
    // What a nozzle holds now (OpenPnP's Nozzle.getPart): the part picked, or "" once placed or discarded.
    virtual void holding(const std::string& /*nozzleId*/, const std::string& /*partId*/) {}
    // Contact probing (JPCell::contactProbeAndWait), and the offsets it keeps
    // by feeder or part (JPCell::probedOffset); none where the machine cannot probe.
    virtual bool contactProbe(const std::string& /*nozzleId*/, bool /*forward*/, double /*depthMm*/, double& /*probedZ*/,
                              std::string& why) {
        why = "this machine cannot contact probe";
        return false;
    }
    virtual std::optional<double> probedOffset(const std::string& /*nozzleId*/, bool /*feeder*/, const std::string& /*key*/) const {
        return std::nullopt;
    }
    // OpenPnP's nozzle tip calibration in a job: whether the tip on a nozzle
    // is calibrated as it should be (its runout measured on it, where it is
    // compensated), and calibrating it.
    virtual bool tipCalibrated(const std::string& /*nozzleId*/) const { return true; }
    virtual bool calibrateTip(const std::string& /*nozzleId*/, std::string& /*why*/) { return true; }
    virtual void setProbedOffset(const std::string& /*nozzleId*/, bool /*feeder*/, const std::string& /*key*/, double /*offsetMm*/) {}
    // The nozzle over `at` at safe Z, turned to its rotation (not down).
    virtual bool positionNozzle(const std::string& nozzleId, const JPLocation& at, std::string& why) = 0;
    // The head camera over `at` (as it is, its height kept).
    virtual bool positionCamera(const JPLocation& at, std::string& why) = 0;
    // A nozzle to `to` (X, Y, Z, rotation in mm and degrees; one not given
    // stays as it is) at `speed` (0..1) of the machine's: up to safe Z and
    // across first when `safeZFirst`, else straight there, every axis at once.
    virtual bool moveNozzle(const std::string& nozzleId, std::array<std::optional<double>, 4> to, double speed, bool safeZFirst,
                            std::string& why) = 0;
    // A nozzle's vacuum on as a pick puts it on (its head's pump too); a pick
    // where it is (the part checked as its tip says); its vacuum level read.
    // Nothing moves.
    virtual bool vacuumOn(const std::string& nozzleId, std::string& why) = 0;
    virtual bool pickHere(const std::string& nozzleId, std::string& why) = 0;
    virtual bool readVacuum(const std::string& nozzleId, double& level, std::string& why) = 0;
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
        // Found by this OpenPnP pipeline (prepared for the fiducial's part; none: not found); `partId`, the
        // fiducial's part, names it in what is said of the look.
        std::shared_ptr<JPPipeline> pipeline;
        std::string                 partId;
    };
    // A fiducial check begins (a board's or panel's, a job's fiducials, a test): the head camera, with its Auto-Tune
    // for fiducial checks? ticked, is tuned on the first fiducial looked at, and that tune kept for the rest of the check.
    virtual void startFiducialCheck() {}
    // The camera over `nominal` (at safe Z), the fiducial found near there by `look`'s pipeline (looked at again,
    // centred, as `look` says), and where it is: `found`. False (and why) without a pipeline, or not found.
    virtual bool locateFiducial(const JPLocation& nominal, const FiducialLook& look, JPLocation& found,
                                std::string& why) = 0;
    // The height (Z) of what a fiducial found at `at` lies on, as OpenPnP's Estimate Object Z measures it: the fiducial
    // looked at from either side of it, along X and along Y, how far it seems to move against how far the camera
    // moved giving the camera's scale there, and the camera's calibration at two heights the height of that scale.
    // False (and why) when the camera is calibrated at one height, or the fiducial is not found.
    virtual bool fiducialHeight(const JPLocation& /*at*/, const FiducialLook& /*look*/, double& /*z*/, std::string& why) {
        why = "this machine cannot tell heights by its camera";
        return false;
    }
    // Bottom vision (OpenPnP's part alignment): the part on the nozzle over
    // the camera looking up, as high as the part (its bottom where the
    // camera is focused), expected at `imageAngle` (the placement's, pre-rotated;
    // else 0); found by its bottom vision settings' OpenPnP pipeline, and its
    // offsets worked out as OpenPnP's findOffsets does (`offsets`: passes,
    // Vision Offset, Max. Pick Tolerance, size check).
    struct AlignRequest {
        double partHeightMm = 0;
        double imageAngle = 0;
        JPBottomVision::Settings offsets;
        // The part's bottom vision settings' pipeline, prepared for the part and its settings.
        std::shared_ptr<JPPipeline> pipeline;
        std::string                 partId, settingsId;
    };
    // Where the part is on the nozzle: with the nozzle turned to `nozzleAngle`,
    // the part's centre less the nozzle's axis (mm), and the part's angle
    // (OpenPnP's PartAlignmentOffset, pre-rotated or not, put so).
    struct AlignResult {
        double nozzleAngle = 0;
        double dx = 0, dy = 0;
        double partAngle = 0;
        double cameraX = 0, cameraY = 0;   // where the camera looking up is
        double partZ = 0;                  // the nozzle's Z it was seen at: the part's bottom at the camera's focus
        // A part of unknown height measured on the way (OpenPnP's auto focus part height): its height.
        std::optional<double> measuredPartHeightMm;
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
    // The QR codes the head camera sees over `at` (its light on, settled): each one's text and where its middle is.
    struct QrCode {
        std::string text;
        JPLocation  at { JPLengthUnit::Millimeters };
    };
    virtual bool readQrCodes(const JPLocation& at, std::vector<QrCode>& codes, std::string& why) = 0;
    // A picture (BGR, as a pipeline's) on the head camera's view for `ms`.
    virtual void showOnCamera(const cv::Mat& bgr, int ms) = 0;
    virtual bool alignPart(const std::string& nozzleId, const AlignRequest& request, AlignResult& result,
                           std::string& why) = 0;
    // A strip's sprocket hole where it should be (`nominal`, its Z the tape's height), as OpenPnP's strip feeder finds
    // it: the strip's `pipeline` run with the head camera over each of `from` (none: over it), at the camera's scale
    // at the tape's height (calibrated at two heights), the round mark nearest it within `searchMm` in each, those
    // averaged. `configure`: the pipeline set up for the feeder once it has the camera (its scale known), before it
    // runs. False (and why): none there.
    virtual bool locateHole(const JPLocation& nominal, JPPipeline& pipeline, double searchMm, const std::vector<JPLocation>& from,
                            const std::function<void(JPPipeline&)>& configure, JPLocation& found, std::string& why) = 0;
};

} // inline namespace jf
