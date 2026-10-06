// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPMountConfig.h"

#include <array>
#include <string>
#include <vector>

inline namespace jf {

// An actuator: an output the cell switches (a light, a valve, a pump) and/or
// a value it reads (a vacuum sensor). Commands are templates sent to the
// actuator's controller; {index} is replaced by `index`. The first group of
// `readPattern` is the value in the reply to `readCommand`.
struct JPActuatorConfig {
    enum class ValueType { Boolean, Number, Text, Profile };

    std::string   id;
    std::string   name;
    std::string   driverId;
    JPMountConfig mount;
    ValueType     valueType = ValueType::Boolean;
    std::string   index;
    std::string   onCommand, offCommand;
    // A Number or Text actuator is set to a value by `valueCommand` ({value}
    // replaced by it); switched on or off, without commands of its own for
    // that, it is set to `onValue` or `offValue`.
    std::string   valueCommand;
    std::string   onValue, offValue;
    std::string   readCommand, readPattern;
    std::string   unit;
    // What it is switched to as the machine's state changes, as in OpenPnP:
    // "ActuateOn", "ActuateOff", or left as it is ("LeaveAsIs"; on connect,
    // "AssumeUnknown" too): once connected, once homed, before disconnecting.
    std::string   enabledActuation = "AssumeUnknown";
    std::string   homedActuation   = "LeaveAsIs";
    std::string   disabledActuation = "LeaveAsIs";
    // OpenPnP's Machine Coordination (JPCell::doCoordinate): before it is
    // actuated "None", "CommandStillstand" or "WaitForStillstand"; after,
    // "None" or "WaitForUnconditionalCoordination"; before it is read, "None"
    // or "WaitForStillstand".
    std::string   coordinatedBeforeActuate = "WaitForStillstand";
    std::string   coordinatedAfterActuate  = "None";
    std::string   coordinatedBeforeRead    = "WaitForStillstand";

    // OpenPnP's actuator profiles (a Profile actuator's): up to kProfileActuators
    // other actuators, and named profiles of a value for each (empty: that one
    // is left as it is). Set to a profile's name, each actuator is set to its
    // value; switched on or off, the profile that is Default ON or Default OFF.
    static constexpr size_t kProfileActuators = 6;
    struct Profile {
        std::string name;
        bool        defaultOn = false, defaultOff = false;
        std::array<std::string, kProfileActuators> values {};
    };
    std::array<std::string, kProfileActuators> profileActuators {};
    // OpenPnP's axis interlock (ActuatorInterlockMonitor): as up to four axes
    // move, the actuator is switched (to signal them moving or standing still,
    // inside or outside their safe zone, parked or not) or read to confirm
    // it is safe to move (a number in range, or text matching), before or
    // after the move; only while `conditionalActuatorId` is in its state (if
    // one is named) and at a speed (share of full) in range.
    struct Interlock {
        bool        enabled = false;   // OpenPnP's Axis Interlock? (its own tab)
        std::string type = "None";
        std::array<std::string, 4> axes {};
        std::string conditionalActuatorId;
        std::string conditionalState = "SwitchedOn";
        double      speedMin = 0, speedMax = 1;
        double      goodMin = 0, goodMax = 0;
        std::string pattern;
        bool        byRegex = false;
        static const std::vector<std::string>& types();
        static const std::vector<std::string>& states();
        bool active() const { return enabled && type != "None"; }
    };
    Interlock interlock;
    // OpenPnP's HttpActuator: switched, set and read by HTTP GETs of these,
    // not through a controller. Off or on with no URL of its own sets "0" or
    // "1" by the parameter URL ({val} the value); a URL the same as last time
    // is not asked again. Read: the lines of the read URL's answer, or of each
    // the regex's "Value" group.
    struct Http {
        bool        on = false;
        std::string onUrl, offUrl, paramUrl, readUrl, regex;
    };
    Http http;
    // OpenPnP's ScriptActuator: switched or set by running this script of the
    // scripts folder (JPScripting), told actuateBoolean, actuateDouble or
    // actuateString; not through a controller. Empty: not one.
    std::string scriptName;
    // OpenPnP's NeoDen4FeederActuator: set to a length, a NeoDen 4 feeder is
    // fed that far (its Feeder ID, at Feed Strength) and its peeler (Peeler
    // ID, at Peel Strength) peels Peel Length % of five times it; through a
    // NeoDen 4 controller (JPNeoden4Link's NEOFEED). Off: not one.
    struct Neoden4Feeder {
        bool on = false;
        int  feederId = 1, peelerId = 1;
        int  feedStrength = 50, peelStrength = 30;
        int  peelLength = 100;   // per cent
        // The command it is set by, {value} the length.
        std::string command() const;
    };
    Neoden4Feeder neoden4Feeder;
    // OpenPnP's ThermistorToLinearSensorActuator: the value read is taken as
    // a temperature (degrees C) and made what a linear sensor wired in its
    // place would read: the resistance a thermistor with these Steinhart-Hart
    // coefficients (A, B, C) has at it, the ADC reading (Maximum Value) it
    // gives below R2 in a divider, that as a voltage (Voltage Reference),
    // times Scale plus Offset. R1 is kept, not used (as in OpenPnP: not yet
    // supported). Off: not one.
    struct Thermistor {
        bool   on = false;
        double a = 0.000722378300319346, b = 0.000216301852054578, c = 9.2641025635702e-08;
        double r1 = 0, r2 = 4700;
        double adcMax = 4095, vRef = 3.3;
        double scale = 1, offset = 0;
        double transform(double celsius) const;
    };
    Thermistor thermistor;
    std::vector<Profile> profiles;
    const Profile* profileNamed(const std::string& profileName) const;
    const Profile* defaultProfile(bool on) const;

    bool canSwitch() const {
        if (!scriptName.empty()) return true;
        if (http.on) return !http.onUrl.empty() || !http.offUrl.empty() || !http.paramUrl.empty();
        if (valueType == ValueType::Profile) return defaultProfile(true) || defaultProfile(false);
        return !onCommand.empty() || !offCommand.empty() || (!valueCommand.empty() && (!onValue.empty() || !offValue.empty()));
    }
    bool canSet() const {
        if (!scriptName.empty()) return valueType != ValueType::Boolean;
        if (neoden4Feeder.on) return true;
        if (http.on) return !http.paramUrl.empty();
        if (valueType == ValueType::Profile) return !profiles.empty();
        return valueType != ValueType::Boolean && !valueCommand.empty();
    }
    bool canRead()   const { return http.on ? !http.readUrl.empty() : !readCommand.empty() && !readPattern.empty(); }

    static JPActuatorConfig fromJson(const JJson& j);
    JJson toJson() const;
};

} // inline namespace jf
