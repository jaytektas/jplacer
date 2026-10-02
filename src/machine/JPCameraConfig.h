// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPCameraCalibration.h"
#include "JPMountConfig.h"

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
        if (!calibrations.empty()) {
            JJson list = JJson::array();
            for (const JPCameraCalibration& k : calibrations) list.push(k.toJson());
            j["calibrations"] = list;
        }
        return j;
    }
};

} // inline namespace jf
