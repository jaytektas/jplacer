// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include <j/config/Json.h>

#include <optional>
#include <string>

inline namespace jf {

// One controller as the cell's configuration describes it.
//
//   {
//     "id": "...", "name": "Gantry",
//     "profile": "auto",                      // or a profile id
//     "link": { "type": "serial", "port": "/dev/ttyACM0", "baud": 115200,
//               "flowControl": "none" | "rtscts" | "xonxoff" },
//          or { "type": "simulated", "simulator": { ... JPSimulatedGrbl ... } },
//     "statusIntervalMs": 100, "commandTimeoutMs": 5000,
//     "identifyTimeoutMs": 1000, "homeTimeoutMs": 60000
//   }
struct JPDriverConfig {
    std::string id;
    std::string name;
    std::string profile = "auto";
    JJson       link;
    int statusIntervalMs  = 100;
    int commandTimeoutMs  = 5000;
    int identifyTimeoutMs = 1000;
    int homeTimeoutMs     = 60000;

    // Nothing, with `error`, when a required field is missing.
    static std::optional<JPDriverConfig> fromJson(const JJson& j, std::string& error);
    JJson toJson() const;
};

} // inline namespace jf
