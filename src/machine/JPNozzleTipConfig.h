// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPChangerStep.h"
#include "JPMachineLocation.h"
#include "JPRunout.h"

#include <j/config/Json.h>

#include <array>
#include <map>
#include <string>
#include <vector>

inline namespace jf {

// A nozzle tip: one of the tips the machine's nozzles take, the diameter of
// its end as the camera looking up sees it (in mm; 0 when not known), by
// which the camera finds it, and how it is loaded onto a nozzle and unloaded
// from one. Which nozzles it fits, and which it is on, the nozzles say
// (JPNozzleConfig).
//
// Loading runs `loadSteps`, taught for this tip on this machine; none: it is
// put on by hand. The first move is approached from safe Z (up, across, then
// down to it), and loading ends at safe Z. Unloading is loading run
// backwards (unloadingSteps), unless the tip has steps of its own for it.
struct JPNozzleTipConfig {
    std::string                id;
    std::string                name;
    double                     diameter = 0;
    std::vector<JPChangerStep> loadSteps;
    bool                       unloadReversesLoad = true;
    std::vector<JPChangerStep> unloadSteps;   // when it does not
    // OpenPnP's Cloning Settings: one tip is the Template (the others' changer
    // steps can be cloned from it, moved by the difference of their first
    // moves); a Locked tip is never cloned to; the rest clone from it.
    bool                       templateTip = false;
    bool                       templateLocked = false;
    // OpenPnP's nozzle tip Z calibration, for a contact probing nozzle
    // (JPNozzleConfig::ContactProbe): the tip probed at its Touch Location
    // (none: not set), and every Z move of the nozzle made by how far it met it
    // from where it should (at most the nozzle's Max Z Offset); when, as
    // "Manual" (Calibrate Now only), "MachineHome" (once homed) or
    // "NozzleTipChange" (once homed and on each load); with Fail Homing, a
    // calibration failing once homed fails the homing.
    std::optional<JPMachineLocation> touchLocation;
    std::string                zCalibrationTrigger = "Manual";
    bool                       zCalibrationFailHoming = true;
    // OpenPnP's changer slot Vision Calibration: the head camera over one of
    // the changer's places ("None", "FirstLocation", "SecondLocation",
    // "ThirdLocation", "LastLocation" or "TouchLocation"), Z Adjust added for
    // its scale, finds the slot by two template pictures of it, empty and
    // occupied (Template Width x Height mm; PNG files named beside the cell
    // file), in a picture Tolerance larger, pass after pass (at most Max.
    // Passes) until one moves less than Precision; under Minimum Score, or
    // further than Tolerance off, it fails. How far off the slot was moves
    // every place of the tip's loading and unloading: worked out when first
    // needed, again on each change with "NozzleTipChange", and forgotten on
    // homing unless "Manual".
    struct VisionCalibration {
        std::string location = "None";
        double      zAdjustMm = 0;
        std::string trigger = "Manual";
        double      templateWidthMm = 10, templateHeightMm = 10;
        double      toleranceMm = 4, precisionMm = 0.7;
        int         maxPasses = 3;
        double      minimumScore = 0.2;
        std::string templateEmpty, templateOccupied;   // file names (or whole paths); empty: none
        std::optional<double> lastScore;               // the last match's (not kept)
        bool on() const { return location != "None"; }
    };
    VisionCalibration          visionCalibration;
    // The place Vision Calibration names, Z Adjust added (none: None, or that place not set).
    std::optional<JPMachineLocation> visionCalibrationPlace() const;
    // OpenPnP's Part Dimensions: the largest part it picks (diameter or
    // diagonal, tolerances in), and how far off a part may be picked
    // (bottom vision accepts a part no further off, and looks no further).
    double                     maxPartDiameterMm = 20;
    double                     maxPickToleranceMm = 1;
    // OpenPnP's Min. Part Diameter (the smallest part it picks: its air bore
    // plus twice the pick tolerance at least) and Max. Part Height (taken for
    // a part whose height is not known, as Dynamic Safe Z lifts it).
    double                     minPartDiameterMm = 0;
    double                     maxPartHeightMm = 5;
    // OpenPnP's Push and Drag Usage: whether it may push and drag (sturdy
    // enough for the side forces), and its outside diameter at its lowest 0.75 mm.
    bool                       pushAndDragAllowed = false;
    double                     diameterLowMm = 0;
    // Waited after a pick or place with this tip, on top of the nozzle's own.
    int                        pickDwellMs = 0;
    // OpenPnP's Place Blow-Off Level: the blow-off at place, when the part's package gives none (0: no blow-off).
    double                     placeBlowOffLevel = 0;
    int                        placeDwellMs = 0;
    // PART DETECTION by the vacuum, as in OpenPnP. After a pick (part on) and
    // after a place (part off), the vacuum level read is checked: by itself
    // ("Absolute": within low..high), or as its change from the level read
    // just before ("Difference": that level within low..high, its change
    // within diffLow..diffHigh). "None": not checked. Part off is read once
    // the valve has been opened for `probingMs` and closed for `dwellMs`.
    struct Sensing {
        std::string method = "None";
        double low = 0, high = 0, diffLow = 0, diffHigh = 0;
    };
    Sensing                    partOn, partOff;
    int                        partOffProbingMs = 0;
    int                        partOffDwellMs = 0;
    // RUNOUT: how its end swings as the nozzle turns, measured with the camera
    // looking up (JPRunout), one measuring for each nozzle it was on (by
    // nozzle id). Compensated in moves while `enabled`. Measured at
    // `divisions` angles round the circle, up to `misdetects` of them allowed
    // to fail, `zOffset` above the camera's focus, finding a round end
    // `visionDiameter` across (0: the tip's diameter).
    struct RunoutCalibration {
        bool   enabled = false;
        int    divisions = 6;
        int    misdetects = 0;
        double zOffset = 0;
        double visionDiameter = 0;
        // OpenPnP's Auto Recalibration: "NozzleTipChange" (on each load, and
        // once homed), "NozzleTipChangeInJob" (forgotten on each load, measured
        // again when a job needs it), "MachineHome" (once homed, and on a load
        // when not yet measured) or "Manual"; with Fail Homing, a calibration
        // failing once homed fails the homing.
        std::string recalibration = "NozzleTipChangeInJob";
        bool   failHoming = true;
        static constexpr int kLeastDivisions = 3, kMostDivisions = 72;
    };
    RunoutCalibration                runoutCalibration;
    // OpenPnP's Background Calibration (JPBackgroundCalibration): how the
    // background round the tip is told from a part, measured while runout is
    // calibrated, and used by bottom vision to mask it (MaskHsv), each range
    // widened by its tolerance; the smallest detail a part has (bottom
    // vision's blur and sampling). Method "None", "Brightness" or
    // "BrightnessAndKeyColor"; HSV with hue 0..255 round the circle.
    struct Background {
        std::string method = "None";
        double      minimumDetailSizeMm = 0.2;
        int         minHue = 0, maxHue = 0, tolHue = 8;
        int         minSaturation = 0, maxSaturation = 0, tolSaturation = 8;
        int         minValue = 0, maxValue = 0, tolValue = 8;
        std::string diagnostics;
    };
    Background                       background;
    std::map<std::string, JPRunout>  runout;
    // The runout to compensate on nozzle `nozzleId`; null when none (or off).
    const JPRunout* runoutOn(const std::string& nozzleId) const {
        if (!runoutCalibration.enabled) return nullptr;
        const auto r = runout.find(nozzleId);
        return r == runout.end() ? nullptr : &r->second;
    }

    // The steps that unload it: its own, or loading backwards. Backwards,
    // each move goes to the place the one before it went to, at the speed
    // of the move it undoes, starting from the last place loading reached;
    // an actuator is switched the other way; a move away from safe Z is
    // undone by going up, then across to where that move started.
    std::vector<JPChangerStep> unloadingSteps() const;
    // OpenPnP's tool changer, as its Tool Changer tab has it: four locations
    // (each one not set is left out), the speed of the move to each from the
    // one before (a share of the machine's; to the First, by way of Safe Z at
    // full speed), and an actuator switched on after each of the first three
    // (off, unloading). It is the loading steps, when they are in this form.
    struct OpenPnpChanger {
        std::array<std::optional<JPMachineLocation>, 4> at;
        std::array<double, 4>                           speed { 1, 1, 1, 1 };
        std::array<std::string, 3>                      post;   // actuator ids; empty: none
    };
    // The loading steps in OpenPnP's form; none when they are jplacer's own.
    std::optional<OpenPnpChanger> openPnpChanger() const;
    // The loading steps made from OpenPnP's form (unloading is loading backwards).
    void setOpenPnpChanger(const OpenPnpChanger& changer);
    // What a clone takes (OpenPnP's Locations?, Z Calibration? and Vision Calibration?).
    struct ClonedParts {
        bool locations = true;           // the loading and unloading steps, and the touch location
        bool zCalibration = true;        // the Z calibration trigger and Fail Homing?
        bool visionCalibration = true;   // the Vision Calibration settings and templates
    };
    // OpenPnP's assignNozzleTipChangerSettings: the template's loading and
    // unloading steps and touch location taken, each place moved by how far
    // this tip's first move is from the template's (a coordinate given in
    // both), and its Z calibration settings, as `parts` says. False, and
    // nothing changed, when this tip is locked, or either has no first move
    // to go by.
    bool cloneChangerFrom(const JPNozzleTipConfig& templateTip, ClonedParts parts);
    bool cloneChangerFrom(const JPNozzleTipConfig& templateTip) { return cloneChangerFrom(templateTip, ClonedParts()); }
    static std::vector<JPChangerStep> reversed(const std::vector<JPChangerStep>& steps);

    // What is wrong with its steps (a list that does not start with a move
    // giving X, Y and Z), in words; empty when sound.
    std::vector<std::string> problems() const;

    static JPNozzleTipConfig fromJson(const JJson& j);
    JJson toJson() const;
};

} // inline namespace jf
