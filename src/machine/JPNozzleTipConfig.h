// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include <j/config/Json.h>

#include <string>

inline namespace jf {

// A nozzle tip: one of the tips the machine's nozzles take, and the diameter
// of its end as the camera looking up sees it (in mm; 0 when not known), by
// which the camera finds it. Which nozzles it fits, and which it is on, the
// nozzles say (JPNozzleConfig).
struct JPNozzleTipConfig {
    std::string id;
    std::string name;
    double      diameter = 0;

    static JPNozzleTipConfig fromJson(const JJson& j) {
        return { j["id"].str(), j["name"].str(), j["diameter"].number() };
    }
    JJson toJson() const {
        JJson j = JJson::object();
        j["id"]       = id;
        j["name"]     = name;
        j["diameter"] = diameter;
        return j;
    }
};

} // inline namespace jf
