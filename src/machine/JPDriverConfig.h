// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include <j/config/Json.h>

#include <map>
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
//     "identifyTimeoutMs": 1000, "homeTimeoutMs": 60000, "connectWaitMs": 1000,
//     "commands": { "home": "M18 Z" }         // this controller's own, over the profile's
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
    // How long to listen after opening the port before asking anything. Many
    // boards greet a new connection (a banner, after a reset or on seeing the
    // host): a question asked before the greeting has arrived gets its answer
    // cut in two by it.
    int connectWaitMs     = 1000;
    // The fastest any move is sent, per minute (0: no cap beyond the axes' own).
    double maxFeedRate    = 0;
    // Every line sent and received goes to the log (else only when tracing).
    bool   logGcode       = false;
    // Commands this controller is sent instead of its profile's (same names:
    // home, move, …). A machine wired its own way homes its own way.
    std::map<std::string, std::string> commands;

    // Nothing, with `error`, when a required field is missing.
    static std::optional<JPDriverConfig> fromJson(const JJson& j, std::string& error);
    JJson toJson() const;
};

} // inline namespace jf
