// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPCameraCalibration.h"
#include "JPMountConfig.h"

#include <string>

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
    JPCameraCalibration calibration;   // jplacer's own, from known moves; .valid false until measured

    static JPCameraConfig fromJson(const JJson& j) {
        JPCameraConfig c;
        c.id             = j["id"].str();
        c.name           = j["name"].str();
        c.looksUp        = j["looking"].str() == "up";
        c.mount          = JPMountConfig::fromJson(j["mount"]);
        c.unitsPerPixelX = j["unitsPerPixel"]["x"].number();
        c.unitsPerPixelY = j["unitsPerPixel"]["y"].number();
        c.device         = j["device"];
        c.calibration    = JPCameraCalibration::fromJson(j["calibration"]);
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
        if (calibration.valid) j["calibration"] = calibration.toJson();
        return j;
    }
};

} // inline namespace jf
