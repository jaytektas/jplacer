// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include <j/config/Json.h>

#include <string>

inline namespace jf {

// OpenPnP's SimulationModeMachine: the machine's imperfections, simulated,
// to try out what is meant to overcome them (calibration, visual homing,
// camera settling) with no hardware, or with the machine's own settings and
// no controller (Replace Drivers?).
//
// IdealMachine: none. StaticImperfectionsMachine: a homing error (the head
// cameras see the machine that far off until visual homing corrects it) and
// non-squareness (X off by the factor times Y). DynamicImperfectionsMachine:
// those, and runout on every nozzle tip (the up-looking cameras see the tip
// go round on that radius from the phase angle), the cameras' lag (what they
// show from that long ago), vibration (a head camera shaking after each move,
// dying away to about 1% over the duration) and noise (that many sparks at
// most in each picture; dark while its light is off).
struct JPSimulationConfig {
    enum class Mode { Off, IdealMachine, StaticImperfectionsMachine, DynamicImperfectionsMachine };

    Mode   mode = Mode::Off;
    bool   replaceDrivers = true;   // every controller simulated while not Off
    double runoutMm = 0, runoutPhaseDeg = 30;
    double nonSquarenessFactor = 0;
    double cameraLagS = 0;
    int    cameraNoise = 0;
    double vibrationAmplitudeMm = 0, vibrationDurationS = 0.2;
    double homingErrorX = 0, homingErrorY = 0;
    // The Z Set Machine Table Z gives the feeders, the boards and the cameras.
    double machineTableZ = 0;
    // The frequency the head rings at (OpenPnP's simulated eigenfrequency).
    static constexpr double kVibrationHz = 13.313;

    bool on() const { return mode != Mode::Off; }
    bool imperfect() const { return mode == Mode::StaticImperfectionsMachine || dynamic(); }
    bool dynamic() const { return mode == Mode::DynamicImperfectionsMachine; }
    bool replacesDrivers() const { return on() && replaceDrivers; }

    static const char* name(Mode m) {
        switch (m) {
            case Mode::IdealMachine:                return "IdealMachine";
            case Mode::StaticImperfectionsMachine:  return "StaticImperfectionsMachine";
            case Mode::DynamicImperfectionsMachine: return "DynamicImperfectionsMachine";
            default:                                return "Off";
        }
    }
    static Mode modeNamed(const std::string& n) {
        for (Mode m : { Mode::IdealMachine, Mode::StaticImperfectionsMachine, Mode::DynamicImperfectionsMachine })
            if (n == name(m)) return m;
        return Mode::Off;
    }

    static JPSimulationConfig fromJson(const JJson& j) {
        JPSimulationConfig s;
        if (!j.isObject()) return s;
        s.mode = modeNamed(j["mode"].str());
        s.replaceDrivers = j["replaceDrivers"].boolean(true);
        s.runoutMm = j["runout"].number(0.0);
        s.runoutPhaseDeg = j["runoutPhase"].number(30.0);
        s.nonSquarenessFactor = j["nonSquarenessFactor"].number(0.0);
        s.cameraLagS = j["cameraLag"].number(0.0);
        s.cameraNoise = int(j["cameraNoise"].number(0));
        s.vibrationAmplitudeMm = j["vibrationAmplitude"].number(0.0);
        s.vibrationDurationS = j["vibrationDuration"].number(0.2);
        s.homingErrorX = j["homingError"]["x"].number(0.0);
        s.homingErrorY = j["homingError"]["y"].number(0.0);
        s.machineTableZ = j["machineTableZ"].number(0.0);
        return s;
    }
    JJson toJson() const {
        JJson j = JJson::object();
        j["mode"] = std::string(name(mode));
        j["replaceDrivers"] = replaceDrivers;
        j["runout"] = runoutMm;
        j["runoutPhase"] = runoutPhaseDeg;
        j["nonSquarenessFactor"] = nonSquarenessFactor;
        j["cameraLag"] = cameraLagS;
        j["cameraNoise"] = cameraNoise;
        j["vibrationAmplitude"] = vibrationAmplitudeMm;
        j["vibrationDuration"] = vibrationDurationS;
        j["homingError"]["x"] = homingErrorX;
        j["homingError"]["y"] = homingErrorY;
        j["machineTableZ"] = machineTableZ;
        return j;
    }
};

} // inline namespace jf
