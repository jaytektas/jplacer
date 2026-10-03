// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPMountConfig.h"

#include <string>

inline namespace jf {

// An actuator: an output the cell switches (a light, a valve, a pump) and/or
// a value it reads (a vacuum sensor). Commands are templates sent to the
// actuator's controller; {index} is replaced by `index`. The first group of
// `readPattern` is the value in the reply to `readCommand`.
struct JPActuatorConfig {
    enum class ValueType { Boolean, Number, Text };

    std::string   id;
    std::string   name;
    std::string   driverId;
    JPMountConfig mount;
    ValueType     valueType = ValueType::Boolean;
    std::string   index;
    std::string   onCommand, offCommand;
    std::string   readCommand, readPattern;
    std::string   unit;
    // What it is switched to as the machine's state changes, as in OpenPnP:
    // "ActuateOn", "ActuateOff", or left as it is ("LeaveAsIs"; on connect,
    // "AssumeUnknown" too): once connected, once homed, before disconnecting.
    std::string   enabledActuation = "AssumeUnknown";
    std::string   homedActuation   = "LeaveAsIs";
    std::string   disabledActuation = "LeaveAsIs";

    bool canSwitch() const { return !onCommand.empty() || !offCommand.empty(); }
    bool canRead()   const { return !readCommand.empty() && !readPattern.empty(); }

    static JPActuatorConfig fromJson(const JJson& j);
    JJson toJson() const;
};

} // inline namespace jf
