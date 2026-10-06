// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPCameraCalibration.h"
#include "JPSettleTrace.h"
#include "JPMountConfig.h"

#include <array>
#include <cmath>
#include <optional>
#include <string>
#include <vector>

inline namespace jf {

// A camera: where it rides, which way it looks, how big a pixel is on the
// work (mm), and the capture device as its backend describes it (kept as
// given; the capture backend reads it).
struct JPCameraConfig {
    std::string   id;
    std::string   name;
    bool          looksUp = false;
    JPMountConfig mount;
    double        unitsPerPixelX = 0, unitsPerPixelY = 0;
    JJson         device;
    // How much of a straightened picture's bent edge is shown: 0 cropped
    // (enlarged until every part has picture behind it) .. 1 whole (all the
    // camera sees). See JPStraightener.
    double        showAll = 0;
    // OpenPnP's image transforms (JPImageTransform): each picture woven from
    // two stacked fields, and cut about its middle to cropWidth x cropHeight
    // (0: that side whole), before anything else is done with it.
    bool          deinterlace = false;
    // OpenPnP's camera Properties: the live picture shown at most this many
    // times a second (0: every picture); held still while the machine works
    // (only what vision shows then); and brought to the front when vision
    // shows a result on it or it is moved to look somewhere.
    double        previewFps = 0;
    // OpenPnP's Roaming Radius (a fixed camera's): how far from the camera's
    // centre a nozzle may carry a part at camera Z (mm), which also bounds
    // the largest part bottom vision can see in several shots
    // (JPVisionComposite); 0: not set, one shot only.
    double        roamingRadiusMm = 0;
    // OpenPnP's Default Working Plane Z (a head camera's): the height of what
    // it looks at when that is not known otherwise, typically the boards'
    // surface. Calibrated at two heights, its scale is taken there (else at
    // the height it was calibrated at). A fixed camera's is its own Z.
    std::optional<double> workingPlaneZ;
    // OpenPnP's Focus Sensing Method (a fixed camera's): "None", or
    // "AutoFocus" (JPAutoFocus): a part of unknown height found in focus
    // above the nozzle. Its Auto Focus tab: the Focal Resolution it narrows
    // down to (mm), pictures averaged at each step, the speed it moves at (a
    // share of the machine's), and whether it shows what it sees.
    std::string   focusSensingMethod = "None";
    struct AutoFocus {
        double focalResolutionMm = 0.05;
        int    averagedFrames = 1;
        double focusSpeed = 0.5;
        bool   showDiagnostics = true;
    };
    AutoFocus     autoFocus;
    // How far above the camera's Z the last auto focus test found focus (while jplacer runs; not kept).
    std::optional<double> lastFocusDistanceMm;
    bool          suspendDuringTasks = false;
    bool          autoCameraView = false;
    // OpenPnP's Show in multi camera view?: off, its dock starts closed
    // (a capture card's camera shown through SwitcherCameras, say); View, or
    // anything that looks through it, opens it.
    bool          shownInMultiView = true;
    // OpenPnP's Capture FPS: how many pictures a second the camera gave over its Test (not kept).
    std::optional<double> captureFps;
    int           cropWidth = 0, cropHeight = 0;
    // SETTLING, as OpenPnP does it: a picture for vision is one taken once
    // the camera has stopped moving. FixedTime waits `timeMs` after the move.
    // The others compare each picture with the one before (Maximum, Mean,
    // Euclidean or Square of the difference, as a percentage of full scale;
    // Motion, how far in pixels the picture moved) in a circle of
    // `maskCircle` of the picture's height (0 the whole picture) until the
    // difference has stayed under `threshold` for `debounce` pictures more,
    // or `timeoutMs` has passed. Each picture is first made grey (unless
    // `fullColor`), its contrast enhanced by `contrastEnhance` (0 none, 1 to
    // the full range), blurred by a Gaussian `gaussianBlur` pixels across
    // (0 none; a large one on a picture scaled down), and with `gradients`
    // its edges taken (Laplacian). `diagnostics`: each settle's pictures and
    // differences kept for its graph and replay.
    struct Settle {
        std::string method = "FixedTime";
        int         timeMs = 150;
        int         timeoutMs = 500;
        double      threshold = 0.5;
        int         debounce = 0;
        double      maskCircle = 0;
        bool        fullColor = false;
        int         gaussianBlur = 0;
        bool        gradients = false;
        double      contrastEnhance = 0;
        bool        diagnostics = false;
    };
    Settle        settle;
    // When it counts as LOST (JPCameraFeed): no picture for `noPictureS`, or
    // the very same picture for `samePictureS` (0: never, for a camera that
    // can show a still scene exactly alike). It is then opened again until
    // it is back; work looking through it waits `waitS` for that (a USB
    // drop-out, opened again in a couple of seconds) before failing, saying
    // the camera was lost (0: fails at once).
    struct Lost {
        int noPictureS   = 3;
        int samePictureS = 3;
        int waitS        = 10;
    };
    Lost          lost;
    // The last settling test (Camera Settling's test buttons), for its graph;
    // not kept in the file.
    std::optional<JPSettleTrace> settleTrace;
    // WHITE BALANCE, as OpenPnP's: each channel (red, green, blue) scaled by
    // its balance, then given its own gamma (JPWhiteBalance).
    // OpenPnP's mapped white balance: per channel, what each of a number of
    // equal brightness levels becomes (0..1), interpolated between; when set,
    // it is used in place of the balance and gamma (which then say roughly
    // what it does).
    struct WhiteBalance {
        std::array<double, 3> balance{ 1, 1, 1 };
        std::array<double, 3> gamma{ 1, 1, 1 };
        std::array<std::vector<double>, 3> maps;
        bool mapped() const { return !maps[0].empty() && maps[0].size() == maps[1].size() && maps[0].size() == maps[2].size(); }
        bool neutral() const {
            return balance == std::array<double, 3>{ 1, 1, 1 } && gamma == std::array<double, 3>{ 1, 1, 1 } && !mapped();
        }
    };
    WhiteBalance  whiteBalance;
    // When its light (device "light-actuator-id") is switched, as in OpenPnP:
    // on before a picture is taken for vision, on while you are looking at
    // the camera; off after the picture for vision, and off while another
    // camera takes one (anti-glare).
    struct Light {
        bool beforeCapture = true;
        bool userAction    = true;
        bool afterCapture  = false;
        bool antiGlare     = false;
    };
    Light         light;
    // How Calibrate measures it (JPCameraCalibrator): a grid of `columns` x
    // `rows` places across the picture, reaching `reach` of the way from the
    // middle to where the edge leaves room for the mark; a measurement further
    // from the fit than `outlierSpread` times the fit's spread (and a pixel)
    // is left out; a fit worse than `maxRmsPx` is refused.
    struct Calibrating {
        int    columns       = 7;
        int    rows          = 5;
        double reach         = 1.0;
        double outlierSpread = 3.0;
        double maxRmsPx      = 1.0;
        // Measured at a second height too: a camera on a head over the
        // head's secondary calibration mark, a fixed one with the nozzle's tip
        // raised `raiseMm` from the first.
        bool   twoHeights    = true;
        double raiseMm       = 2.0;
        // Each place come to the same way: from `leadInMm` back along both
        // axes (so the drives' play is taken up alike every time, whatever
        // their compensation); and found in `frames` pictures, their mean.
        double leadInMm      = 1.0;
        int    frames        = 4;
        static constexpr int kMostFrames = 16;
        static constexpr int kMostPlaces = 25;   // across or down
    };
    Calibrating   calibrating;
    // jplacer's own, from known moves: one for each picture size it was
    // measured at (another size is another scale, and another lens).
    std::vector<JPCameraCalibration> calibrations;

    // An image camera's (OpenPnP's ImageCamera, device backend "image")
    // picture scale when it gives none (mm a pixel).
    static constexpr double kDefaultImageUnitsPerPixel = 0.04;
    // OpenPnP's simulated cameras (an ImageCamera, a SimulatedUpCamera) not
    // calibrated here: taken as OpenPnP takes them, at their Units Per Pixel,
    // straight (looking up, mirrored, as a camera looking up sees); none for
    // another camera (it must be calibrated).
    std::optional<JPCameraCalibration> openPnpCalibration(int width, int height) const {
        const bool image = device["backend"].str() == "image";
        const bool simulatedUp = device["backend"].str() == "simulated" && device["openpnpClass"].str() == "SimulatedUpCamera";
        if (!image && !simulatedUp) return std::nullopt;
        const double ux = unitsPerPixelX > 0 ? unitsPerPixelX : device["imageUnitsPerPixel"]["x"].number(kDefaultImageUnitsPerPixel);
        const double uy = unitsPerPixelY > 0 ? unitsPerPixelY : device["imageUnitsPerPixel"]["y"].number(kDefaultImageUnitsPerPixel);
        if (ux <= 0 || uy <= 0) return std::nullopt;
        JPCameraCalibration c;
        c.valid = true;
        c.width = width;
        c.height = height;
        // A mark at P is seen at the middle + M (V - P), V where the camera looks.
        c.pxPerMm = { (looksUp ? 1 : -1) / ux, 0, 0, 1 / uy };
        c.lensCentreX = width / 2.0;
        c.lensCentreY = height / 2.0;
        return c;
    }
    // The calibration for pictures width x height; null when there is none.
    const JPCameraCalibration* calibrationFor(int width, int height) const {
        for (const JPCameraCalibration& c : calibrations)
            if (c.width == width && c.height == height) return &c;
        return nullptr;
    }
    // Keep `c`, in place of one at the same size.
    void keepCalibration(const JPCameraCalibration& c) {
        for (JPCameraCalibration& k : calibrations)
            if (k.width == c.width && k.height == c.height) {
                k = c;
                return;
            }
        calibrations.push_back(c);
    }

    // The actuator that lights what it looks at; empty when it has none.
    std::string lightActuator() const { return device["light-actuator-id"].str(); }

    static JPCameraConfig fromJson(const JJson& j) {
        JPCameraConfig c;
        c.id             = j["id"].str();
        c.name           = j["name"].str();
        c.looksUp        = j["looking"].str() == "up";
        c.mount          = JPMountConfig::fromJson(j["mount"]);
        c.unitsPerPixelX = j["unitsPerPixel"]["x"].number();
        c.unitsPerPixelY = j["unitsPerPixel"]["y"].number();
        c.device         = j["device"];
        c.showAll        = j["showAll"].number(0.0);
        c.deinterlace    = j["deinterlace"].boolean();
        c.previewFps     = j["previewFps"].number(0.0);
        c.roamingRadiusMm = j["roamingRadius"].number(0.0);
        if (j["workingPlaneZ"].isNumber()) c.workingPlaneZ = j["workingPlaneZ"].number();
        if (!j["focusSensingMethod"].str().empty()) c.focusSensingMethod = j["focusSensingMethod"].str();
        if (const JJson& f = j["autoFocus"]; f.isObject()) {
            c.autoFocus.focalResolutionMm = f["focalResolution"].number(c.autoFocus.focalResolutionMm);
            c.autoFocus.averagedFrames    = int(f["averagedFrames"].number(c.autoFocus.averagedFrames));
            c.autoFocus.focusSpeed        = f["focusSpeed"].number(c.autoFocus.focusSpeed);
            c.autoFocus.showDiagnostics   = f["showDiagnostics"].boolean(true);
        }
        c.suspendDuringTasks = j["suspendDuringTasks"].boolean();
        c.autoCameraView = j["autoCameraView"].boolean();
        c.shownInMultiView = j["shownInMultiView"].boolean(true);
        c.cropWidth      = int(j["crop"]["width"].number(0.0));
        c.cropHeight     = int(j["crop"]["height"].number(0.0));
        for (size_t ch = 0; ch < 3; ++ch) {
            c.whiteBalance.balance[ch] = j["whiteBalance"]["balance"][ch].number(1.0);
            c.whiteBalance.gamma[ch]   = j["whiteBalance"]["gamma"][ch].number(1.0);
            for (const JJson& v : j["whiteBalance"]["maps"][ch].arr()) c.whiteBalance.maps[ch].push_back(v.number());
        }
        if (const JJson& l = j["light"]; l.isObject()) {
            c.light.beforeCapture = l["beforeCapture"].boolean(c.light.beforeCapture);
            c.light.userAction    = l["userAction"].boolean(c.light.userAction);
            c.light.afterCapture  = l["afterCapture"].boolean(c.light.afterCapture);
            c.light.antiGlare     = l["antiGlare"].boolean(c.light.antiGlare);
        }
        if (const JJson& st = j["settle"]; st.isObject()) {
            if (const std::string& m = st["method"].str(); !m.empty()) c.settle.method = m;
            c.settle.timeMs     = int(st["timeMs"].number(c.settle.timeMs));
            c.settle.timeoutMs  = int(st["timeoutMs"].number(c.settle.timeoutMs));
            c.settle.threshold  = st["threshold"].number(c.settle.threshold);
            c.settle.debounce   = int(st["debounce"].number(c.settle.debounce));
            c.settle.maskCircle = st["maskCircle"].number(c.settle.maskCircle);
            c.settle.fullColor  = st["fullColor"].boolean(false);
            c.settle.gaussianBlur = int(st["gaussianBlur"].number(0));
            c.settle.gradients  = st["gradients"].boolean(false);
            c.settle.contrastEnhance = st["contrastEnhance"].number(0);
            c.settle.diagnostics = st["diagnostics"].boolean(false);
        }
        if (const JJson& l = j["lost"]; l.isObject()) {
            c.lost.noPictureS   = int(l["noPictureS"].number(c.lost.noPictureS));
            c.lost.samePictureS = int(l["samePictureS"].number(c.lost.samePictureS));
            c.lost.waitS        = int(l["waitS"].number(c.lost.waitS));
        }
        if (const JJson& k = j["calibrating"]; k.isObject()) {
            c.calibrating.columns       = int(k["columns"].number(c.calibrating.columns));
            c.calibrating.rows          = int(k["rows"].number(c.calibrating.rows));
            c.calibrating.reach         = k["reach"].number(c.calibrating.reach);
            c.calibrating.outlierSpread = k["outlierSpread"].number(c.calibrating.outlierSpread);
            c.calibrating.maxRmsPx      = k["maxRmsPx"].number(c.calibrating.maxRmsPx);
            c.calibrating.twoHeights    = k["twoHeights"].boolean(c.calibrating.twoHeights);
            c.calibrating.raiseMm       = k["raiseMm"].number(c.calibrating.raiseMm);
            c.calibrating.leadInMm      = k["leadInMm"].number(c.calibrating.leadInMm);
            c.calibrating.frames        = int(k["frames"].number(c.calibrating.frames));
        }
        for (const JJson& k : j["calibrations"].arr())
            if (JPCameraCalibration cal = JPCameraCalibration::fromJson(k); cal.valid) c.calibrations.push_back(cal);
        return c;
    }
    JJson toJson() const {
        JJson j = JJson::object();
        j["id"]                 = id;
        j["name"]               = name;
        j["looking"]            = looksUp ? "up" : "down";
        j["mount"]              = mount.toJson();
        j["unitsPerPixel"]["x"] = unitsPerPixelX;
        j["unitsPerPixel"]["y"] = unitsPerPixelY;
        j["device"]             = device;
        j["showAll"]            = showAll;
        if (deinterlace) j["deinterlace"] = true;
        if (previewFps > 0) j["previewFps"] = previewFps;
        if (roamingRadiusMm > 0) j["roamingRadius"] = roamingRadiusMm;
        if (workingPlaneZ) j["workingPlaneZ"] = *workingPlaneZ;
        if (focusSensingMethod != "None") j["focusSensingMethod"] = focusSensingMethod;
        j["autoFocus"]["focalResolution"] = autoFocus.focalResolutionMm;
        j["autoFocus"]["averagedFrames"]  = autoFocus.averagedFrames;
        j["autoFocus"]["focusSpeed"]      = autoFocus.focusSpeed;
        j["autoFocus"]["showDiagnostics"] = autoFocus.showDiagnostics;
        if (suspendDuringTasks) j["suspendDuringTasks"] = true;
        if (autoCameraView) j["autoCameraView"] = true;
        if (!shownInMultiView) j["shownInMultiView"] = false;
        if (cropWidth || cropHeight) {
            j["crop"]["width"] = cropWidth;
            j["crop"]["height"] = cropHeight;
        }
        if (!whiteBalance.neutral()) {
            JJson balance = JJson::array(), gamma = JJson::array();
            for (size_t ch = 0; ch < 3; ++ch) {
                balance.push(JJson(whiteBalance.balance[ch]));
                gamma.push(JJson(whiteBalance.gamma[ch]));
            }
            j["whiteBalance"]["balance"] = balance;
            j["whiteBalance"]["gamma"]   = gamma;
            if (whiteBalance.mapped()) {
                JJson maps = JJson::array();
                for (size_t ch = 0; ch < 3; ++ch) {
                    JJson m = JJson::array();
                    for (double v : whiteBalance.maps[ch]) m.push(JJson(v));
                    maps.push(m);
                }
                j["whiteBalance"]["maps"] = maps;
            }
        }
        j["light"]["beforeCapture"] = light.beforeCapture;
        j["light"]["userAction"]    = light.userAction;
        j["light"]["afterCapture"]  = light.afterCapture;
        j["light"]["antiGlare"]     = light.antiGlare;
        j["settle"]["method"]     = settle.method;
        j["settle"]["timeMs"]     = settle.timeMs;
        j["settle"]["timeoutMs"]  = settle.timeoutMs;
        j["settle"]["threshold"]  = settle.threshold;
        j["settle"]["debounce"]   = settle.debounce;
        j["settle"]["maskCircle"] = settle.maskCircle;
        j["settle"]["fullColor"]  = settle.fullColor;
        j["settle"]["gaussianBlur"] = settle.gaussianBlur;
        j["settle"]["gradients"]  = settle.gradients;
        j["settle"]["contrastEnhance"] = settle.contrastEnhance;
        j["settle"]["diagnostics"] = settle.diagnostics;
        j["lost"]["noPictureS"]   = lost.noPictureS;
        j["lost"]["samePictureS"] = lost.samePictureS;
        j["lost"]["waitS"]        = lost.waitS;
        j["calibrating"]["columns"]       = calibrating.columns;
        j["calibrating"]["rows"]          = calibrating.rows;
        j["calibrating"]["reach"]         = calibrating.reach;
        j["calibrating"]["outlierSpread"] = calibrating.outlierSpread;
        j["calibrating"]["maxRmsPx"]      = calibrating.maxRmsPx;
        j["calibrating"]["twoHeights"]    = calibrating.twoHeights;
        j["calibrating"]["raiseMm"]       = calibrating.raiseMm;
        j["calibrating"]["leadInMm"]      = calibrating.leadInMm;
        j["calibrating"]["frames"]        = calibrating.frames;
        if (!calibrations.empty()) {
            JJson list = JJson::array();
            for (const JPCameraCalibration& k : calibrations) list.push(k.toJson());
            j["calibrations"] = list;
        }
        return j;
    }
};

} // inline namespace jf
