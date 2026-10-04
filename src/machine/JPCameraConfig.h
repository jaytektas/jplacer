// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPCameraCalibration.h"
#include "JPSettleTrace.h"
#include "JPMountConfig.h"

#include <array>
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
    // SETTLING, as OpenPnP does it: a picture for vision is one taken once
    // the camera has stopped moving. FixedTime waits `timeMs` after the move.
    // The others compare each picture with the one before (Maximum, Mean,
    // Euclidean or Square of the difference, as a percentage of full scale,
    // in a circle of `maskCircle` of the picture's height, 0 the whole
    // picture) until the difference has stayed under `threshold` for
    // `debounce` pictures more, or `timeoutMs` has passed.
    struct Settle {
        std::string method = "FixedTime";
        int         timeMs = 150;
        int         timeoutMs = 500;
        double      threshold = 0.5;
        int         debounce = 0;
        double      maskCircle = 0;
    };
    Settle        settle;
    // The last settling test (Camera Settling's test buttons), for its graph;
    // not kept in the file.
    std::optional<JPSettleTrace> settleTrace;
    // WHITE BALANCE, as OpenPnP's: each channel (red, green, blue) scaled by
    // its balance, then given its own gamma (JPWhiteBalance).
    struct WhiteBalance {
        std::array<double, 3> balance{ 1, 1, 1 };
        std::array<double, 3> gamma{ 1, 1, 1 };
        bool neutral() const { return balance == std::array<double, 3>{ 1, 1, 1 } && gamma == std::array<double, 3>{ 1, 1, 1 }; }
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
        for (size_t ch = 0; ch < 3; ++ch) {
            c.whiteBalance.balance[ch] = j["whiteBalance"]["balance"][ch].number(1.0);
            c.whiteBalance.gamma[ch]   = j["whiteBalance"]["gamma"][ch].number(1.0);
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
        if (!whiteBalance.neutral()) {
            JJson balance = JJson::array(), gamma = JJson::array();
            for (size_t ch = 0; ch < 3; ++ch) {
                balance.push(JJson(whiteBalance.balance[ch]));
                gamma.push(JJson(whiteBalance.gamma[ch]));
            }
            j["whiteBalance"]["balance"] = balance;
            j["whiteBalance"]["gamma"]   = gamma;
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
