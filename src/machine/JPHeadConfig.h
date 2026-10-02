// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include <j/config/Json.h>

#include <string>

inline namespace jf {

// A head: the moving carriage nozzles, cameras and actuators are mounted on
// (JPMountConfig::headId names it).
struct JPHeadConfig {
    std::string id;
    std::string name;

    static JPHeadConfig fromJson(const JJson& j) { return { j["id"].str(), j["name"].str() }; }
    JJson toJson() const {
        JJson j = JJson::object();
        j["id"]   = id;
        j["name"] = name;
        return j;
    }
};

} // inline namespace jf
