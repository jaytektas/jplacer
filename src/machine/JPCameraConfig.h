// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPCameraCalibration.h"
#include "JPMountConfig.h"

#include <array>
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
    // WHITE BALANCE, as OpenPnP's: each channel (red, green, blue) scaled by
    // its balance, then given its own gamma (JPWhiteBalance).
    struct WhiteBalance {
        std::array<double, 3> balance{ 1, 1, 1 };
        std::array<double, 3> gamma{ 1, 1, 1 };
        bool neutral() const { return balance == std::array<double, 3>{ 1, 1, 1 } && gamma == std::array<double, 3>{ 1, 1, 1 }; }
    };
    WhiteBalance  whiteBalance;
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
            c.whiteBalance.balance[ch] = j["whiteBalance"]["balance"][ch].number(1);
            c.whiteBalance.gamma[ch]   = j["whiteBalance"]["gamma"][ch].number(1);
        }
        if (const JJson& st = j["settle"]; st.isObject()) {
            if (const std::string& m = st["method"].str(); !m.empty()) c.settle.method = m;
            c.settle.timeMs     = int(st["timeMs"].number(c.settle.timeMs));
            c.settle.timeoutMs  = int(st["timeoutMs"].number(c.settle.timeoutMs));
            c.settle.threshold  = st["threshold"].number(c.settle.threshold);
            c.settle.debounce   = int(st["debounce"].number(c.settle.debounce));
            c.settle.maskCircle = st["maskCircle"].number(c.settle.maskCircle);
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
        j["settle"]["method"]     = settle.method;
        j["settle"]["timeMs"]     = settle.timeMs;
        j["settle"]["timeoutMs"]  = settle.timeoutMs;
        j["settle"]["threshold"]  = settle.threshold;
        j["settle"]["debounce"]   = settle.debounce;
        j["settle"]["maskCircle"] = settle.maskCircle;
        if (!calibrations.empty()) {
            JJson list = JJson::array();
            for (const JPCameraCalibration& k : calibrations) list.push(k.toJson());
            j["calibrations"] = list;
        }
        return j;
    }
};

} // inline namespace jf
