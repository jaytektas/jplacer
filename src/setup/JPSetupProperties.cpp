// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPSetupProperties.h"

#include "camera/JPOnvif.h"
#include "camera/JPSimulatedUpCamera.h"
#include "camera/JPWhiteBalance.h"

#include "JPVisionForms.h"

#include "JPFormBuilder.h"
#include "JPSetupTree.h"

#include "machine/JPNeoden4Link.h"
#include "machine/JPTcpLink.h"

#include <algorithm>
#include <cstdio>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <memory>
#include <optional>
#include <utility>

inline namespace jf {

namespace {

using Strings = std::vector<std::string>;

// A nozzle tip's vacuum graph: the valve's scale (0 closed, 1 open) padded
// so it runs in a band low down (as OpenPnP's scales).
constexpr double kValveBandBelow = 0.25, kValveBandAbove = 4.0;
// A camera's settling graph: the pictures' capture ticks in a band low down, as OpenPnP's.
constexpr double kCaptureBandBelow = 0.1, kCaptureBandAbove = 6.0;
// A picture and a difference this close in time (ms) are the same picture's.
constexpr double kSameMs = 0.5;

template <class T>
JPFormBuilder::Named named(const std::vector<T>& items, const std::string& none) {
    JPFormBuilder::Named n;
    if (!none.empty()) n.add(none, "");
    for (const T& i : items) n.add(i.name.empty() ? i.id : i.name, i.id);
    return n;
}


template <class T>
std::function<T&()> finder(std::vector<T>& items, const std::string& id) {
    return [&items, id]() -> T& {
        for (T& i : items)
            if (i.id == id) return i;
        return items.front();   // not reached: a form is made only for a part in the cell
    };
}

template <class T>
bool has(const std::vector<T>& items, const std::string& id) {
    for (const T& i : items)
        if (i.id == id) return true;
    return false;
}

using Place = JPSetupProperties::Place;
const Strings kXYZR{ "X", "Y", "Z", "Rotation" };

// Where a nozzle, camera or actuator is: on which head (or none), moved by
// which axes, and its offset there: OpenPnP's Coordinate System group, the
// axes and offsets as columns. `noHead`: what having none is called; a
// fixed part with a place (a camera looking up) has a Location instead.
template <class T>
void coordinateSystem(JPFormBuilder& add, JPCellConfig& cell, std::function<T&()> part, const std::string& noHead,
                      bool fixedHasPlace, JPSetupProperties::Form& form) {
    auto mount = [part]() -> JPMountConfig& { return part().mount; };
    add.group(mount().headId.empty() && fixedHasPlace ? "Location" : "Coordinate System");
    add.byName("head", "Head", named(cell.heads, noHead), [mount] { return mount().headId; },
               [&cell, mount](const std::string& headId) {
                   JPMountConfig& m = mount();
                   m.headId = headId;
                   if (headId.empty()) {
                       m.axisX = m.axisY = m.axisZ = m.axisRotation = "";
                       return;
                   }
                   // On a head it moves with the head: on X and Y as the head's other parts do.
                   if (!m.axisX.empty()) return;
                   auto from = [&m](const auto& items) {
                       for (const auto& i : items)
                           if (i.mount.headId == m.headId && &i.mount != &m && !i.mount.axisX.empty()) {
                               m.axisX = i.mount.axisX;
                               m.axisY = i.mount.axisY;
                               return true;
                           }
                       return false;
                   };
                   from(cell.nozzles) || from(cell.cameras) || from(cell.actuators);
               });
    form.reshaping.push_back("head");
    if (mount().headId.empty()) {
        if (!fixedHasPlace) return;
        // Where it is on the machine; Z where a part is in focus over it.
        add.header({ "X", "Y", "Z" });
        add.row("Location", Place::Location);
        add.length("offsetX", "X", [mount]() -> double& { return mount().offsetX; });
        add.length("offsetY", "Y", [mount]() -> double& { return mount().offsetY; });
        add.length("offsetZ", "Z", [mount]() -> double& { return mount().offsetZ; });
        add.end();
        return;
    }
    const JPFormBuilder::Named axes = named(cell.axes, "(none)");
    add.header(kXYZR);
    add.row("Axis");
    add.byName("axisX", "X axis", axes, [mount]() -> std::string& { return mount().axisX; });
    add.byName("axisY", "Y axis", axes, [mount]() -> std::string& { return mount().axisY; });
    add.byName("axisZ", "Z axis", axes, [mount]() -> std::string& { return mount().axisZ; });
    add.byName("axisRotation", "Rotation axis", axes, [mount]() -> std::string& { return mount().axisRotation; });
    add.end();
    add.row("Offset");
    add.length("offsetX", "Offset X", [mount]() -> double& { return mount().offsetX; });
    add.length("offsetY", "Offset Y", [mount]() -> double& { return mount().offsetY; });
    add.length("offsetZ", "Offset Z", [mount]() -> double& { return mount().offsetZ; });
    add.end();
}

// OpenPnP's ReferenceAdvancedMotionPlanner tabs: its settings and Test Motion
// places, and the diagnostics of the last test run (`test`, none yet).
void motionPlannerTabs(JPCellConfig& cell, JPFormBuilder& add, const JPMotionTestResult* test) {
    auto mp = [&cell]() -> JPMotionPlannerConfig& { return cell.motionPlanner; };
    add.tab("Motion Planner");
    add.group("Motion Planner");
    add.flag("allowContinuousMotion", "Allow continous motion?", [mp]() -> bool& { return mp().continuousMotion; });
    add.tip("Often, jplacer directs the controller(s) to execute motion that involves multiple segments. For example, "
            "consider a move to Safe Z, followed by a move over the target location, followed by a move to lower the "
            "nozzle down to pick or place a part. If the controller gets these commands as one sequence, there are no "
            "delays introduced when communicating back and forth. By allowing continuous motion, the planner no longer "
            "waits for motion to complete each time, unless explicitly told to (an actuator's Machine Coordination, a "
            "pick or place, the end of each operation).");
    add.group("Test Motion");
    add.header({ "X", "Y", "Z", "Rotation", "Enabled?" });
    static const char* const kStops[] = { "First Location", "Second Location", "Third Location", "Last Location" };
    for (size_t i = 0; i < 4; ++i) {
        auto at = [mp, i]() -> JPMachineLocation& { return mp().stops[i].at; };
        const std::string n = "testMotion" + std::to_string(i + 1);
        add.row(kStops[i], JPFormBuilder::Place::Location);
        add.positionNoSafeZ();
        add.length(n + "X", std::string(kStops[i]) + " X", [at]() -> double& { return at().x; });
        add.length(n + "Y", std::string(kStops[i]) + " Y", [at]() -> double& { return at().y; });
        add.length(n + "Z", std::string(kStops[i]) + " Z", [at]() -> double& { return at().z; });
        add.number(n + "Rotation", std::string(kStops[i]) + " Rotation", [at]() -> double& { return at().rotation; });
        add.flag(n + "Enabled", std::string(kStops[i]) + " Enabled?", [mp, i]() -> bool& { return mp().stops[i].enabled; });
        add.end();
    }
    add.header({ "Speed", "Safe Z?" });
    static const char* const kLegs[] = { "Speed 1 ↔ 2", "Speed 2 ↔ 3", "Speed 3 ↔ 4" };
    static const char* const kLegTips[] = { "Speed between First location and Second location",
                                            "Speed between Second location and Third location",
                                            "Speed between Third location and Last location" };
    for (size_t i = 0; i < 3; ++i) {
        add.row(kLegs[i]);
        add.number("testMotionSpeed" + std::to_string(i + 1), kLegs[i], [mp, i]() -> double& { return mp().speeds[i]; });
        add.tip(kLegTips[i]);
        add.flag("testMotionSafeZ" + std::to_string(i + 1), std::string(kLegs[i]) + " Safe Z?",
                 [mp, i]() -> bool& { return mp().safeZ[i]; });
        add.end();
    }
    add.note("CAUTION! Test Motion moves the tool chosen on the Jog panel through the enabled locations, straight "
             "where Safe Z? is off: make sure the way between them is clear.");

    add.tab("Motion Planner Diagnostics");
    add.group("");
    // On one row, as OpenPnP's.
    auto seconds = [test](double JPMotionTestResult::*field) {
        return std::function<std::string()>([test, field] {
            if (!test) return std::string();
            char text[32];
            std::snprintf(text, sizeof text, "%.3f", test->*field);
            return std::string(text);
        });
    };
    add.row("Diagnostics?");
    add.flag("diagnosticsEnabled", "Diagnostics?", [mp]() -> bool& { return mp().diagnosticsEnabled; });
    add.tip("Record where each axis is over a Test Motion run, from the controllers' reports, and show it below.");
    add.button("testMotion", "Test", "Test the motion defined in the Motion Planner tab.");
    add.text("moveTimePlanned", "Planned [s]", seconds(&JPMotionTestResult::plannedS), nullptr);
    add.tip("How long the run's moves take as planned from the axes' feed rates and accelerations.");
    add.text("moveTimeActual", "Actual", seconds(&JPMotionTestResult::actualS), nullptr);
    add.tip("How long the run took, from leaving the first location to standing still at the last.");
    add.end();
    if (!test || !cell.motionPlanner.diagnosticsEnabled || test->axes.empty()) {
        add.note("no data");
        return;
    }
    // Each axis's place over the run, and its speed between reports.
    auto location = std::make_shared<JPPlot>();
    auto velocity = std::make_shared<JPPlot>();
    location->xTitle = velocity->xTitle = "s";
    location->yTitle = "mm, °";
    velocity->yTitle = "mm/s, °/s";
    static const JPPlot::Tone kTones[] = { JPPlot::Tone::First, JPPlot::Tone::Second, JPPlot::Tone::Third, JPPlot::Tone::Muted };
    size_t k = 0;
    for (const auto& [name, points] : test->axes) {
        JPPlot::Series l { name, kTones[k % 4], {} }, v { name, kTones[k % 4], {} };
        ++k;
        for (size_t i = 0; i < points.size(); ++i) {
            l.points.push_back({ points[i].first, points[i].second });
            if (i > 0 && points[i].first > points[i - 1].first)
                v.points.push_back({ (points[i].first + points[i - 1].first) / 2,
                                     (points[i].second - points[i - 1].second) / (points[i].first - points[i - 1].first) });
        }
        location->series.push_back(std::move(l));
        velocity->series.push_back(std::move(v));
    }
    add.plot("Location", location);
    add.plot("Velocity", velocity);
}

// OpenPnP's SimulationModeMachine tab: the machine's imperfections, simulated.
void simulationTab(JPCellConfig& cell, JPFormBuilder& add, JPSetupProperties::Form& f) {
    using S = JPSimulationConfig;
    auto sim = [&cell]() -> S& { return cell.simulation; };
    add.tab("Simulation Mode");
    add.group("Simulation Mode");
    add.choice("simulationMode", "Simulation Mode",
               { "Off", "IdealMachine", "StaticImperfectionsMachine", "DynamicImperfectionsMachine" },
               [sim] { return std::string(S::name(sim().mode)); }, [sim](const std::string& v) { sim().mode = S::modeNamed(v); });
    f.reshaping.push_back("simulationMode");
    add.flag("simulationReplaceDrivers", "Replace Drivers?", [sim]() -> bool& { return sim().replaceDrivers; });
    add.tip("Replace driver connections with a built-in simulated controller, simulating the real driver. Will only "
            "become effective when you connect the machine again.");
    add.note("IdealMachine: none of the imperfections below. StaticImperfectionsMachine: the homing error and the "
             "non-squareness. DynamicImperfectionsMachine: all of them. They show on the simulated cameras: a head "
             "camera sees the machine through them, an up-looking one sees the nozzle tips go round on the runout.");
    add.group("Imperfections");
    add.row("Nozzle Tip Runout");
    add.length("simulatedRunout", "Nozzle Tip Runout", [sim]() -> double& { return sim().runoutMm; });
    add.end();
    add.tip("Simulates runout of that radius on all nozzle tips.");
    add.number("simulatedRunoutPhase", "Runout Phase", [sim]() -> double& { return sim().runoutPhaseDeg; });
    add.tip("Phase angle for the simulated runout.");
    add.note("Be aware that runout will be apparent as an offset in the cross-hairs of the Down-looking Camera, "
             "whenever the Nozzle is positioned. This also happens when watching a Job perform.");
    add.number("simulatedNonSquareness", "Non-Squareness Factor", [sim]() -> double& { return sim().nonSquarenessFactor; }, 6);
    add.tip("Creates simulated Non-Squareness by that factor.");
    add.flag("pickAndPlaceChecking", "Pick & Place Checking?", [sim]() -> bool& { return sim().pickAndPlaceChecking; });
    add.note("Pick & Place Checking: with an image camera on the head (OpenPnP's test picture), each pick must find a "
             "part's body there, and each place its pads, within the camera's tolerances, or the job stops.");
    add.number("simulatedCameraLag", "Camera Lag [s]", [sim]() -> double& { return sim().cameraLagS; });
    add.integer("simulatedCameraNoise", "Camera Noise", [sim]() -> int& { return sim().cameraNoise; }, 0, 100000);
    add.tip("Creates simulated noise in the camera image (number of sparks) to satisfy Camera Settling that the frame "
            "has changed.");
    add.length("simulatedVibrationAmplitude", "Vibration Amplitude", [sim]() -> double& { return sim().vibrationAmplitudeMm; });
    add.tip("Simulates Vibration: a head camera shakes along each move as it stops, by this much at first, at the "
            "machine's Eigenfrequency (13.3 Hz).");
    add.number("simulatedVibrationDuration", "Vibration Duration [s]", [sim]() -> double& { return sim().vibrationDurationS; });
    add.tip("Vibration duration in seconds (exponential decay to ~1%).");
    add.header({ "X", "Y" });
    add.row("Homing Error");
    add.length("simulatedHomingErrorX", "Homing Error X", [sim]() -> double& { return sim().homingErrorX; });
    add.length("simulatedHomingErrorY", "Homing Error Y", [sim]() -> double& { return sim().homingErrorY; });
    add.end();
    add.endColumns();
    add.tip("Simulates an initial homing error by that offset. Used to test visual homing.");
    add.group("Locations");
    add.row("Machine Table Z");
    add.length("machineTableZ", "Machine Table Z", [sim]() -> double& { return sim().machineTableZ; });
    add.button("setMachineTableZ", "Set Machine Table Z", "Gives the feeders, the job's boards and the cameras this Z.");
    add.end();
    add.actions({ { "Reset Feeders", "resetFeeders" } });
    add.tip("Sets the feed count of every strip and blinds feeder back to 0.");
}

void machineForm(JPCellConfig& cell, JPSetupProperties::Form& f, const JPMotionTestResult* motionTest) {
    f.title = "Machine";
    JPFormBuilder add(f);
    add.tab("Configuration");
    add.group("General");
    add.text("name", "Name", [&cell]() -> std::string& { return cell.name; }, "name");
    add.flag("parkAfterHome", "Park after homed?", [&cell]() -> bool& { return cell.parkAfterHome; });
    add.flag("safeZPark", "Park all at Safe Z?", [&cell]() -> bool& { return cell.safeZPark; });
    add.tip("When the Z Park button is pressed, move all tools mounted on the same head to safe Z.");
    add.length("unsafeZRoaming", "Unsafe Z Roaming", [&cell]() -> double& { return cell.unsafeZRoamingMm; }, 2);
    add.tip("Maximum allowable roaming distance at unsafe Z. Virtual Z axes (typically on cameras) are invisible, therefore "
            "it can easily be overlooked that you are at unsafe Z. Jogging further away will automatically move the "
            "virtual axis to Safe Z.");
    add.flag("autoToolSelect", "Auto tool select?", [&cell]() -> bool& { return cell.autoToolSelect; });
    add.tip("Whenever an explicit user action is performed on a tool, automatically select it in Machine Controls.");
    add.flag("poolScriptingEngines", "Pool scripting engines?", [&cell]() -> bool& { return cell.poolScriptingEngines; });
    add.tip("Python and JavaScript scripts are run by interpreters kept from one script to the next, not started anew "
            "each time: faster, but what a script leaves behind (a module's state) is there for the next.");
    add.flag("autoLoadMostRecentJob", "Auto-load most recent job?", [&cell]() -> bool& { return cell.autoLoadMostRecentJob; });
    add.group("Locations");
    add.header({ "X", "Y", "Z", "Rotation", "Set?" });
    auto at = [&cell]() -> std::optional<JPMachineLocation>& { return cell.discardLocation; };
    add.row("Discard Location", at() ? Place::Location : Place::None);
    if (at()) {
        add.length("discardX", "Discard X", [at]() -> double& { return at()->x; });
        add.length("discardY", "Discard Y", [at]() -> double& { return at()->y; });
        add.length("discardZ", "Discard Z", [at]() -> double& { return at()->z; });
        add.number("discardRotation", "Discard Rotation", [at]() -> double& { return at()->rotation; });
    } else {
        for (int i = 0; i < 4; ++i) add.skip();
    }
    add.flag("discard", "Set?", [at] { return at().has_value(); },
             [at](bool on) {
                 if (!on) at().reset();
                 else if (!at()) at() = JPMachineLocation();
             });
    add.end();
    f.reshaping.push_back("discard");
    auto board = [&cell]() -> JPMachineLocation& { return cell.defaultBoardLocation; };
    add.row("Default Board Location", Place::Location);
    add.length("defaultBoardX", "Default Board X", [board]() -> double& { return board().x; });
    add.length("defaultBoardY", "Default Board Y", [board]() -> double& { return board().y; });
    add.length("defaultBoardZ", "Default Board Z", [board]() -> double& { return board().z; });
    add.number("defaultBoardRotation", "Default Board Rotation", [board]() -> double& { return board().rotation; });
    add.skip();   // always set: nothing under Set?
    add.end();
    add.note("Discard Location: where a nozzle drops a part that is not wanted. Default Board Location: where a "
             "board or panel added to a job starts.");
    motionPlannerTabs(cell, add, motionTest);
    simulationTab(cell, add, f);
}

void driverForm(JPCellConfig& cell, const std::string& id, const std::vector<JPFirmwareProfile>& profiles, JPSetupProperties::Form& f) {
    auto d = finder(cell.drivers, id);
    f.title = "Controller " + d().name;
    JPFormBuilder add(f);
    add.tab("Configuration");
    add.group("Properties");
    add.text("name", "Name", [d]() -> std::string& { return d().name; }, "name");
    Strings choices{ "auto" };
    for (const JPFirmwareProfile& p : profiles) choices.push_back(p.id());
    add.choice("profile", "Firmware Profile", choices, [d] { return d().profile; },
               [d](const std::string& v) { d().profile = v; });
    add.flag("homeAfterConnect", "Home after connected?", [d]() -> bool& { return d().homeAfterConnect; });
    // A simulated controller (OpenPnP's NullDriver) is for trying jplacer
    // without a machine; made a serial or TCP one here (or by Issues &
    // Solutions' Replace with GcodeDriver), it drives a real controller.
    if (std::as_const(d().link)["type"].str() == "simulated") {
        add.group("Communications");
        add.choice("communicationsType", "Communications Type", { "simulated", "serial", "tcp" }, [] { return std::string("simulated"); },
                   [d](const std::string& v) {
                       if (v == "simulated") return;
                       d().link = JJson::object();
                       d().link["type"] = v;
                   });
        f.reshaping.push_back("communicationsType");
        add.note("Simulated: a controller jplacer imitates, to try it without a machine. Serial or TCP: the real one.");
    } else {
        add.group("Communications");
        auto linkText = [d](const char* key, const std::string& none) {
            return [d, key, none] { const std::string v = std::as_const(d().link)[key].str(); return v.empty() ? none : v; };
        };
        // OpenPnP's Communications Type: a serial port, or TCP; or a NeoDen 4
        // (OpenPnP's NeoDen4Driver: its own protocol on a serial port, its
        // firmware profile the neoden4 one).
        add.choice("communicationsType", "Communications Type", { "serial", "tcp", "neoden4" }, linkText("type", "serial"),
                   [d](const std::string& v) {
                       d().link["type"] = v;
                       if (v == "neoden4") d().profile = "neoden4";
                   });
        f.reshaping.push_back("communicationsType");
        const bool neoden4 = std::as_const(d().link)["type"].str() == "neoden4";
        if (!neoden4)
            add.choice("lineEnding", "Line-Endings", { "LF", "CR", "CRLF" }, linkText("lineEnding", "LF"),
                       [d](const std::string& v) { d().link["lineEnding"] = v; });
        if (neoden4) {
            // OpenPnP's NeoDen4Driver settings: where X and Y home (the axes'
            // home coordinates), and the scale of their steps.
            add.group("NeoDen 4");
            constexpr int kScaleDecimals = 8;   // OpenPnP's Y scale has eight places
            for (const auto& [type, key, label] : { std::tuple { JPAxisConfig::Type::X, "homeCoordinateX", "Home Coordinate X" },
                                                    std::tuple { JPAxisConfig::Type::Y, "homeCoordinateY", "Home Coordinate Y" } }) {
                auto axis = [&cell, d, type]() -> JPAxisConfig* {
                    for (JPAxisConfig& a : cell.axes)
                        if (a.kind == JPAxisConfig::Kind::Controller && a.driverId == d().id && a.type == type) return &a;
                    return nullptr;
                };
                if (axis()) add.length(key, label, [axis]() -> double& { return axis()->homeCoordinate; });
            }
            add.number("scaleX", "Scale Factor - X", [d] { return std::as_const(d().link)["scaleX"].number(JPNeoden4Link::kScaleX); },
                       [d](double v) { d().link["scaleX"] = v; }, kScaleDecimals);
            add.number("scaleY", "Scale Factor - Y", [d] { return std::as_const(d().link)["scaleY"].number(JPNeoden4Link::kScaleY); },
                       [d](double v) { d().link["scaleY"] = v; }, kScaleDecimals);
            add.tip("The machine's steps are hundredths of a millimetre, times these.");
        }
        if (std::as_const(d().link)["type"].str() == "tcp") {
            add.group("TCP");
            add.text("host", "IP Address", [d] { return std::as_const(d().link)["host"].str(); },
                     [d](const std::string& v) { d().link["host"] = v; }, "long");
            add.tip("IP address or host-name.");
            add.integer("tcpPort", "Port", [d] { return int(std::as_const(d().link)["port"].number(JPTcpLink::kDefaultPort)); },
                        [d](int v) { d().link["port"] = v; }, 1, 65535);
        } else {
            add.group("Serial Port");
            add.text("port", "Port", [d] { return std::as_const(d().link)["port"].str(); },
                     [d](const std::string& v) { d().link["port"] = v; }, "long");
            // The rates a serial port takes.
            Strings rates;
            for (int r : { 1200, 4800, 9600, 19200, 38400, 57600, 115200, 230400, 921600 }) rates.push_back(std::to_string(r));
            add.choice("baud", "Baud", rates, [d] { return std::to_string(int(std::as_const(d().link)["baud"].number())); },
                       [d](const std::string& v) { d().link["baud"] = std::atoi(v.c_str()); });
            add.choice("parity", "Parity", { "none", "even", "odd" }, linkText("parity", "none"),
                       [d](const std::string& v) { d().link["parity"] = v; });
            add.choice("dataBits", "Data Bits", { "5", "6", "7", "8" },
                       [d] { return std::to_string(int(std::as_const(d().link)["dataBits"].number(8))); },
                       [d](const std::string& v) { d().link["dataBits"] = std::atoi(v.c_str()); });
            add.choice("stopBits", "Stop Bits", { "1", "2" },
                       [d] { return std::to_string(int(std::as_const(d().link)["stopBits"].number(1))); },
                       [d](const std::string& v) { d().link["stopBits"] = std::atoi(v.c_str()); });
            add.choice("flowControl", "Flow Control", { "none", "rtscts", "xonxoff" }, linkText("flowControl", "none"),
                       [d](const std::string& v) { d().link["flowControl"] = v == "none" ? std::string() : v; });
            add.flag("setDtr", "Set DTR", [d] { return std::as_const(d().link)["setDtr"].boolean(); },
                     [d](bool v) { d().link["setDtr"] = v; });
            add.flag("setRts", "Set RTS", [d] { return std::as_const(d().link)["setRts"].boolean(); },
                     [d](bool v) { d().link["setRts"] = v; });
        }
        add.note("Connection settings are used the next time the machine is connected.");
    }
    add.tab("Driver Settings");
    add.group("Settings");
    add.number("maxFeedRate", "Max. Feed Rate [/min]", [d]() -> double& { return d().maxFeedRate; }, 0);
    add.flag("logGcode", "Log G-code?", [d]() -> bool& { return d().logGcode; });
    add.choice("units", "Units", { "Millimeters", "Inches" }, [d] { return d().units; }, [d](const std::string& v) { d().units = v; });
    add.tip("The units of the controller's G-code: coordinates, feed rate, acceleration and jerk (rotations stay degrees). "
            "Its connect command must say so to it (G20 for inches, G21 for millimetres).");
    add.flag("letterVariables", "Letter Variables?", [d]() -> bool& { return d().usingLetterVariables; });
    add.tip("Axis variables in Gcode are named using the Axis Letters rather than the Axis Type. Off: a move command names "
            "them {X} {Y} {Z} {Rotation}, one axis of each a command.");
    add.flag("preMove", "Allow Pre-Move Commands?", [d]() -> bool& { return d().supportingPreMove; });
    add.tip("Each moving axis's Pre-Move Command is sent before the move, {Coordinate} where the axis was. Only with "
            "Letter Variables off.");
    add.flag("backslashEscapes", "Backslash Escaped Characters?", [d]() -> bool& { return d().backslashEscapes; });
    add.tip("Allows insertion of unicode characters into Gcode strings as \\uxxxx where xxxx is four hexidecimal "
            "characters.  Also permits \\t for tab, \\b for backspace, \\n for line feed, \\r for carriage return, "
            "and \\f for form feed.");
    add.flag("removeComments", "Remove Comments?", [d]() -> bool& { return d().removeComments; });
    add.tip("Remove comments from G-code to speed up transmissions to the controller.");
    add.flag("compressGcode", "Compress G-code?", [d]() -> bool& { return d().compressGcode; });
    add.tip("Remove unneeded white-space and trailing decimal digits from G-code to speed up transmissions to the "
            "controller: G1 X100.0000 Y20.1000 is sent as G1X100Y20.1.");
    for (const auto& [key, label, s] : { std::tuple { "sendOnChangeFeed", "Send FeedRate On Change Only?", &JPDriverConfig::sendOnChangeFeed },
                                         std::tuple { "sendOnChangeAcceleration", "Send Acceleration On Change Only?", &JPDriverConfig::sendOnChangeAcceleration },
                                         std::tuple { "sendOnChangeJerk", "Send Jerk On Change Only?", &JPDriverConfig::sendOnChangeJerk } }) {
        add.flag(key, label, [d, s]() -> bool& { return (d().*s).on; });
        add.tip("A move's value left out (with its letter) when within the relative deviation of the one last sent, "
                "as the controller keeps it; sent again after connecting and homing.");
    }
    add.text("compressionExcludes", "Compression Exclude Characters", [d]() -> std::string& { return d().compressionExcludes; });
    add.tip("Anything between the left-most and right-most of these characters is left out of compression and comments "
            "removal (quotes, brackets); with only one of them, the rest of the line.");
    add.integer("commandTimeoutMs", "Command Timeout [ms]", [d]() -> int& { return d().commandTimeoutMs; }, 100, 600000);
    add.integer("connectWaitMs", "Connect Wait Time [ms]", [d]() -> int& { return d().connectWaitMs; }, 0, 60000);
    add.flag("keepAlive", "Keep Alive", [d]() -> bool& { return d().keepAlive; });
    add.tip("Keep the connection open when the machine is disconnected, and take it up again as it is on the next "
            "connect (a controller that resets as its port is opened is not reset again).");
    add.integer("identifyTimeoutMs", "Identify Timeout [ms]", [d]() -> int& { return d().identifyTimeoutMs; }, 100, 60000);
    add.integer("dollarWaitMs", "$-Command Wait Time [ms]", [d]() -> int& { return d().dollarWaitMs; }, 0, 60000);
    add.tip("After a command beginning with $ (a grbl setting, written to its EEPROM) is confirmed, the next waits this long.");
    add.integer("homeTimeoutMs", "Home Timeout [ms]", [d]() -> int& { return d().homeTimeoutMs; }, 1000, 600000);
    add.integer("statusIntervalMs", "Status Interval [ms]", [d]() -> int& { return d().statusIntervalMs; }, 10, 10000);
    add.note("Max. Feed Rate 0: moves are as fast as their axes allow.");
    // OpenPnP's Detect Firmware, and what the controller said.
    add.group("Firmware");
    add.button("detectFirmware", "Detect Firmware", "Ask the connected controller again what firmware it runs.");
    add.text("detectedFirmware", "Firmware", [d] { return d().detectedFirmware.empty() ? std::string("(not asked yet: connect)") : d().detectedFirmware; },
             nullptr, "lines");

    // The firmware's commands, each replaceable for this controller (a
    // machine wired its own way homes its own way). Empty: the profile's.
    add.tab("Gcode");
    // The profiles' templates for each command (the profile chosen, or every
    // one when it is found by itself), and the commands this controller has.
    std::map<std::string, std::vector<std::pair<std::string, std::string>>> templates;   // command: (profile, text)
    for (const JPFirmwareProfile& p : profiles)
        if (d().profile == "auto" || d().profile == p.id())
            for (const auto& [name, text] : p.commands()) templates[name].push_back({ p.id(), text });
    for (const auto& [name, _] : d().commands) templates[name];
    // What each is for, by what it does.
    struct Command { const char* name; const char* group; const char* label; };
    static const Command known[] = {
        { "init", "Connecting", "Start-up" },          { "unlock", "Connecting", "Unlock" },
        { "home", "Homing", "Home" },                  { "setPosition", "Homing", "Set Position" },
        { "move", "Moving", "Move" },                  { "rapid", "Moving", "Rapid Move" },
        { "waitMotion", "Moving", "Wait for Moves" },  { "dwell", "Moving", "Dwell" },
        { "output", "Outputs", "Output On" },          { "outputOff", "Outputs", "Output Off" },
    };
    static const std::pair<const char*, const char*> groups[] = {
        { "Connecting", "Start-up is sent once connected (and again after an alarm is unlocked); Unlock clears an "
                        "alarm before homing." },
        { "Homing", "Set Position tells the controller where the axes are, after homing or a correction: {axes} is "
                    "each axis's letter and coordinate." },
        { "Moving", "{axes}: each moving axis's letter and target. {feed}: the speed (per minute). {acceleration} "
                    "and {jerk}: the slowest of the axes', scaled with the speed. Wait for Moves is answered once "
                    "every move before it has ended. Dwell: {seconds} or {milliseconds}." },
        { "Outputs", "{port}: the output's number." },
        { "Other", "" },
    };
    // An empty box shows the profile's command; found by itself (auto), each
    // profile's, named, those that agree named together.
    const bool named = d().profile == "auto";
    auto placeholder = [named](const std::vector<std::pair<std::string, std::string>>& those) {
        std::vector<std::pair<std::string, std::string>> merged;   // (profiles, text)
        for (const auto& [profile, text] : those) {
            auto same = std::find_if(merged.begin(), merged.end(), [&](const auto& m) { return m.second == text; });
            if (same == merged.end()) merged.push_back({ profile, text });
            else same->first += ", " + profile;
        }
        std::string def;
        for (const auto& [profiles, text] : merged) {
            std::string oneLine = text;
            std::replace(oneLine.begin(), oneLine.end(), '\n', ' ');
            def += (def.empty() ? "" : " \xC2\xB7 ") + (named ? profiles + ": " : std::string()) + oneLine;
        }
        return def;
    };
    for (const auto& [group, about] : groups) {
        std::vector<std::pair<std::string, std::string>> rows;   // (command, label)
        for (const auto& [name, _] : templates) {
            const Command* k = nullptr;
            for (const Command& c : known)
                if (name == c.name) k = &c;
            if ((k ? std::string(k->group) : std::string("Other")) == group) rows.push_back({ name, k ? k->label : name });
        }
        if (rows.empty()) continue;
        // In the order listed above, the others by name.
        auto order = [&](const std::string& n) {
            for (size_t i = 0; i < std::size(known); ++i)
                if (n == known[i].name) return int(i);
            return int(std::size(known));
        };
        std::stable_sort(rows.begin(), rows.end(), [&](const auto& a, const auto& b) { return order(a.first) < order(b.first); });
        add.group(group);
        for (const auto& [name, label] : rows)
            add.text("command:" + name, label,
                     [d, name = name] { const auto i = d().commands.find(name); return i == d().commands.end() ? std::string() : i->second; },
                     [d, name = name](const std::string& v) {
                         if (v.empty()) d().commands.erase(name);
                         else d().commands[name] = v;
                     }, "lines", placeholder(templates[name]));
        if (*about) add.note(about);
    }
    add.note("Empty: the firmware profile's command, shown greyed. A command can be several lines.");
    // OpenPnP's: the controller's settings, its commands with them, written out whole.
    add.group("Import / Export");
    add.button("gcode:export", "Export Gcode File", "Export the Gcode profile to a file.");
    add.button("gcode:copy", "Copy Gcode to Clipboard", "Copy the Gcode profile to the clipboard.");
}

// What measuring an axis's backlash found, as graphs.
void backlashResults(JPFormBuilder& add, const JPBacklashCalibration& k) {
    add.group("Calibrated " + k.when);
    char b[96];
    std::snprintf(b, sizeof b, "%.4f mm", k.toleranceMm);
    add.text("backlashTolerance", "Tolerance", [v = std::string(b)] { return v; }, nullptr);
    add.note("Three times how far the measuring wanders standing still.");
    auto distance = std::make_shared<JPPlot>();
    distance->kind = JPPlot::Kind::Lines;
    distance->logX = true;
    distance->xTitle = "mm come in";
    distance->yTitle = "play mm";
    distance->series.push_back({ "play", JPPlot::Tone::First, {} });
    for (const auto& [d, p] : k.byDistance) distance->series[0].points.push_back({ d, p });
    add.plot("Play Against How Far It Came In", distance);
    add.note("A short way in from the other side takes up only part of the play; from far enough, all of it. "
             "Where the line levels off is how far a move must sneak up.");
    auto speed = std::make_shared<JPPlot>();
    speed->kind = JPPlot::Kind::Lines;
    speed->xTitle = "speed factor";
    speed->yTitle = "play mm";
    speed->series.push_back({ "play", JPPlot::Tone::First, {} });
    for (const auto& [v, p] : k.bySpeed) speed->series[0].points.push_back({ v, p });
    add.plot("Play Against Speed", speed);
    add.note("Level: the play is the same at any speed, and Directional compensation is enough. Falling at speed: "
             "the drive overshoots, and the last of a move must sneak up slowly.");
    auto after = std::make_shared<JPPlot>();
    after->kind = JPPlot::Kind::Points;
    after->xTitle = "mm come in from";
    after->yTitle = "error mm";
    after->series.push_back({ "error", JPPlot::Tone::Second, {} });
    for (const auto& [d, e] : k.after) after->series[0].points.push_back({ d, e });
    add.plot("Errors Once Compensated", after);
    add.note("Moves in to the mark from random places either side (and from afar on each side), compensated, each "
             "against the mean of them all: each should land within the tolerance of the others.");
}

void axisForm(JPCellConfig& cell, const std::string& id, JPSetupProperties::Form& f) {
    using A = JPAxisConfig;
    auto a = finder(cell.axes, id);
    f.title = "Axis " + a().name;
    JPFormBuilder add(f);
    // A linear axis's coordinates, speeds and play are lengths (in the System
    // Units); a rotational one's, degrees.
    const bool linear = a().type != A::Type::Rotation;
    auto lin = [&add, linear](const std::string& name, const std::string& label, std::function<double()> get,
                               std::function<void(double)> set, int decimals) {
        if (linear) add.length(name, label, std::move(get), std::move(set), decimals);
        else add.number(name, label, std::move(get), std::move(set), decimals);
    };
    auto linRef = [lin](const std::string& name, const std::string& label, std::function<double&()> ref, int decimals = 3) {
        lin(name, label, [ref] { return ref(); }, [ref](double v) { ref() = v; }, decimals);
    };
    add.tab("Configuration");
    add.group("Properties");
    // A controller axis is one a controller drives; a mapped one follows
    // another through a straight-line map (a second Z driven the other way);
    // a virtual one is only a number jplacer keeps.
    // A cam one is OpenPnP's cam axis: a Z a cam turned by a rotation axis drives.
    // A linear one is OpenPnP's linear transform: its inputs times their factors, plus an offset.
    const Strings kinds{ "controller", "mapped", "cam", "linear", "virtual" };
    add.choice("kind", "Kind", kinds, [a] { return std::string(A::kindName(a().kind)); },
               [a](const std::string& v) {
                   for (A::Kind k : { A::Kind::Controller, A::Kind::Mapped, A::Kind::Cam, A::Kind::Linear, A::Kind::Virtual })
                       if (v == A::kindName(k)) a().kind = k;
               });
    f.reshaping.push_back("kind");
    add.choice("type", "Type", { "x", "y", "z", "rotation" }, [a] { return std::string(A::typeName(a().type)); },
               [a](const std::string& v) {
                   for (A::Type t : { A::Type::X, A::Type::Y, A::Type::Z, A::Type::Rotation })
                       if (v == A::typeName(t)) a().type = t;
               });
    f.reshaping.push_back("type");   // lengths or degrees
    add.text("name", "Name", [a]() -> std::string& { return a().name; }, "name");
    if (a().kind == A::Kind::Controller) {
        add.group("Controller Settings");
        add.byName("driver", "Driver", named(cell.drivers, "(none)"), [a]() -> std::string& { return a().driverId; });
        add.text("letter", "Axis Letter", [a]() -> std::string& { return a().letter; });
        add.text("preMoveCommand", "Pre-Move Command", [a]() -> std::string& { return a().preMoveCommand; }, "long");
        add.tip("Sent before a move of this axis when its controller allows pre-move commands (and Letter Variables is "
                "off), {Coordinate} where the axis was: to switch an output shared by several axes to this one.");
        linRef("homeCoordinate", "Home Coordinate", [a]() -> double& { return a().homeCoordinate; });
        add.flag("switchLinearRotational", "Switch Linear \u2194 Rotational?", [a]() -> bool& { return a().switchLinearRotational; });
        add.tip("It is important that jplacer understands whether an Axis is linear or rotational in the controller. Most "
                "of the times this is already determined by the Axis Type, i.e. X, Y, Z are linear and Rotation is "
                "rotational. But sometimes you may run out of proper axes on the controller and then have to use a linear "
                "controller axis for a rotational axis or vice versa. If you cannot configure your controller to switch "
                "this meaning, it is important to enable the Switch Linear \u2194 Rotational checkbox. This is relevant "
                "in computing proper limits for feed-rate, acceleration and jerk in mixed axes moves, as only the motion "
                "of linear axes is taken into consideration for the limits in standard G-Code.");
        // One motor step, and its other side: steps per unit.
        const std::string unit = !linear ? "Degree" : JPSystemUnits::inches() ? "Inch" : "Millimeter";
        add.row(std::string("Resolution [") + (!linear ? "Degrees" : JPSystemUnits::inches() ? "Inches" : "Millimeters") + "]");
        linRef("resolution", "Resolution", [a]() -> double& { return a().resolution; }, 6);
        // Steps a unit: one over a step, in the units shown.
        add.number("stepsPerUnit", "Steps / " + unit,
                   [a, linear] {
                       const double step = linear ? JPSystemUnits::shown(a().resolution) : a().resolution;
                       return step > 0 ? 1 / step : 0.0;
                   },
                   [a, linear](double v) {
                       const double step = v > 0 ? 1 / v : 0;
                       a().resolution = linear ? JPSystemUnits::stored(step) : step;
                   }, 6);
        add.end();
        if (a().type == A::Type::Rotation) {
            add.flag("limitRotation", "Limit to Range", [a]() -> bool& { return a().limitRotation; });
            add.flag("wrapAroundRotation", "Wrap Around", [a]() -> bool& { return a().wrapAroundRotation; });
            add.note("Limit to Range keeps the angle within -180..180; Wrap Around turns the short way round.");
        }
    }
    if (a().kind == A::Kind::Virtual) {
        add.group("Virtual Axis");
        linRef("homeCoordinate", "Home / Safe Z", [a]() -> double& { return a().homeCoordinate; });
    }
    if (a().kind == A::Kind::Mapped) {
        add.group("Axis Mapping");
        add.byName("inputAxis", "Input Axis", named(cell.axes, "(none)"), [a]() -> std::string& { return a().inputAxisId; });
        add.header({ "Input", "Output" });
        add.row("Map Point A");
        linRef("mapInput0", "Point A input", [a]() -> double& { return a().mapInput0; });
        linRef("mapOutput0", "Point A output", [a]() -> double& { return a().mapOutput0; });
        add.end();
        add.row("Map Point B");
        linRef("mapInput1", "Point B input", [a]() -> double& { return a().mapInput1; });
        linRef("mapOutput1", "Point B output", [a]() -> double& { return a().mapOutput1; });
        add.end();
        linRef("homeCoordinate", "Home Coordinate", [a]() -> double& { return a().homeCoordinate; });
    }
    if (a().kind == A::Kind::Linear) {
        // OpenPnP's ReferenceLinearTransformAxis.
        add.group("Linear Transformation");
        add.header({ "Input Axis", "Factor" });
        static const char* const kInputs[] = { "X", "Y", "Z", "Rotation" };
        for (size_t i = 0; i < 4; ++i) {
            const std::string key = std::string("linear") + kInputs[i];
            add.row(kInputs[i]);
            add.byName(key + "Input", std::string(kInputs[i]) + " Input Axis", named(cell.axes, "(none)"),
                       [a, i]() -> std::string& { return a().linearInputs[i]; });
            add.number(key + "Factor", std::string(kInputs[i]) + " Factor", [a, i]() -> double& { return a().linearFactors[i]; }, 6);
            add.end();
        }
        add.endColumns();
        linRef("linearOffset", "Offset", [a]() -> double& { return a().linearOffset; });
        add.note("The coordinate is the X input's times its factor, plus the Y input's times its factor, and so on, plus "
                 "the Offset. Moved to, the linear axes of a move are solved back onto their inputs together.");
    }
    if (a().kind == A::Kind::Cam) {
        // OpenPnP's ReferenceCamCounterClockwiseAxis (and its clockwise partner, here the same axis turned the other way).
        add.group("Cam Settings");
        add.byName("inputAxis", "Input Axis", named(cell.axes, "(none)"), [a]() -> std::string& { return a().inputAxisId; });
        add.flag("camClockwise", "Clockwise?", [a]() -> bool& { return a().camClockwise; });
        add.tip("The cam's other side: the nozzle that goes down as the cam turns clockwise (OpenPnP's ReferenceCamClockwiseAxis).");
        add.length("camRadius", "Cam Radius", [a]() -> double& { return a().camRadius; });
        add.number("camArmsAngle", "Cam Arms Angle", [a]() -> double& { return a().camArmsAngle; });
        add.tip("The angle between the cam's two arms (180 for a straight cam); the balance point is at 0 with them folded out.");
        add.length("camWheelRadius", "Cam Wheel Radius", [a]() -> double& { return a().camWheelRadius; });
        add.length("camWheelGap", "Cam Wheel Gap", [a]() -> double& { return a().camWheelGap; });
        linRef("homeCoordinate", "Home Coordinate", [a]() -> double& { return a().homeCoordinate; });
        add.note("Z = Cam Radius x sin(angle + 90 - Cam Arms Angle / 2) + Cam Wheel Radius + Cam Wheel Gap, the angle that "
                 "of the input axis (the other way, clockwise), kept within the cam's useful range.");
    }
    add.group("Kinematic Settings");
    // Each limit with its switch, and buttons to take it from where the axis is or go there.
    auto limit = [&add, a, id, linRef](const std::string& key, const std::string& label, double A::*value, bool A::*on) {
        add.row(label, Place::Axis, id);
        linRef(key, label, [a, value]() -> double& { return a().*value; });
        add.flag(key + "Enabled", "Enabled?", [a, on]() -> bool& { return a().*on; });
        add.end();
    };
    limit("softLimitLow", "Soft Limit Low", &A::softLimitLow, &A::softLimitLowEnabled);
    limit("safeZoneLow", "Safe Zone Low", &A::safeZoneLow, &A::safeZoneLowEnabled);
    limit("safeZoneHigh", "Safe Zone High", &A::safeZoneHigh, &A::safeZoneHighEnabled);
    limit("softLimitHigh", "Soft Limit High", &A::softLimitHigh, &A::softLimitHighEnabled);
    add.row("Feed Rate [/s]");
    linRef("feedratePerSecond", "Feed Rate [/s]", [a]() -> double& { return a().feedratePerSecond; }, 1);
    lin("feedratePerMinute", "Feed Rate [/min]", [a] { return a().feedratePerSecond * 60; },
        [a](double v) { a().feedratePerSecond = v / 60; }, 1);
    add.end();
    linRef("accelerationPerSecond2", "Acceleration [/s\u00B2]", [a]() -> double& { return a().accelerationPerSecond2; }, 1);
    linRef("jerkPerSecond3", "Jerk [/s\u00B3]", [a]() -> double& { return a().jerkPerSecond3; }, 1);
    add.note("0: the controller's own. Acceleration and jerk reach a controller whose move command takes "
             "{acceleration} or {jerk} (its Gcode tab).");

    add.tab("Backlash Compensation");
    add.group("Backlash Compensation");
    // OpenPnP's names for the methods.
    static const std::pair<A::Backlash, const char*> methods[] = {
        { A::Backlash::None, "None" }, { A::Backlash::OneSided, "OneSidedPositioning" },
        { A::Backlash::OneSidedOptimized, "OneSidedOptimizedPositioning" },
        { A::Backlash::Directional, "DirectionalCompensation" }, { A::Backlash::DirectionalSneakUp, "DirectionalSneakUp" },
        { A::Backlash::DistanceAware, "DistanceAware" } };
    Strings names;
    for (const auto& [m, n] : methods) names.push_back(n);
    add.choice("backlash", "Compensation Method", names,
               [a] {
                   for (const auto& [m, n] : methods)
                       if (m == a().backlash) return std::string(n);
                   return std::string("None");
               },
               [a](const std::string& v) {
                   for (const auto& [m, n] : methods)
                       if (v == n) a().backlash = m;
               });
    f.reshaping.push_back("backlash");
    const A::Backlash method = a().backlash;
    if (method == A::Backlash::DistanceAware) {
        lin("approachMm", "Least Approach", [a] { return a().approachMm; },
            [a](double v) { if (v >= 0) a().approachMm = v; }, 3);
        add.number("backlashSpeedFactor", "Speed Factor", [a]() -> double& { return a().backlashSpeedFactor; }, 2);
    } else if (method != A::Backlash::None) {
        linRef("backlashOffset", "Backlash Offset", [a]() -> double& { return a().backlashOffset; });
        if (method == A::Backlash::DirectionalSneakUp)
            lin("sneakUp", "Sneak-up Distance", [a] { return a().sneakUpMm; },
                [a](double v) { if (v >= 0) a().sneakUpMm = v; }, 3);
        if (method != A::Backlash::Directional)
            add.number("backlashSpeedFactor", "Speed Factor", [a]() -> double& { return a().backlashSpeedFactor; }, 2);
    }
    switch (method) {
        case A::Backlash::None:
            add.note("No compensation: where the drive's play leaves it.");
            break;
        case A::Backlash::OneSided:
            add.note("Every move ends the same way: to the place plus the offset first (its sign says which side), "
                     "then in to the place at the speed factor, so the last stretch is always the same. The offset "
                     "need only be at least the play.");
            break;
        case A::Backlash::OneSidedOptimized:
            add.note("As OneSidedPositioning, but a move already arriving the right way goes straight in: fewer "
                     "moves, the last stretch as long as the move.");
            break;
        case A::Backlash::Directional:
            add.note("A move travelling the way the offset points goes the offset further, taking up the play; the "
                     "other way, it goes to the place. The offset must be the play itself.");
            break;
        case A::Backlash::DirectionalSneakUp:
            add.note("As DirectionalCompensation, the last Sneak-up Distance of each move made at the speed factor, "
                     "so it cannot overshoot.");
            break;
        case A::Backlash::DistanceAware:
            add.note(a().backlashTable.empty()
                         ? "jplacer's own: the lag measured for each distance travelled since the axis last turned, "
                           "sent each move. Calibrate measures it; until then nothing is compensated."
                         : "jplacer's own: each move is sent the lag measured for how far the axis will have "
                           "travelled since it last turned (the graph below, half the play); one that would come in "
                           "less than the Least Approach first backs off that far, then comes in at the speed factor.");
            break;
    }
    if (a().kind == A::Kind::Controller && (a().type == A::Type::X || a().type == A::Type::Y)) {
        add.actions({ { "Calibrate", "calibrateBacklash" } });
        add.note("Calibrate measures the play with the head camera over the homing fiducial (the machine homed, the "
                 "camera calibrated): standing still for the tolerance, then coming in from either side over "
                 "distances and at speeds, then chooses the method and tries it with moves from random places.");
    }
    if (const auto& k = a().backlashCalibration) backlashResults(add, *k);
}

void headForm(JPCellConfig& cell, const std::string& id, JPSetupProperties::Form& f) {
    auto h = finder(cell.heads, id);
    f.title = "Head " + h().name;
    JPFormBuilder add(f);
    add.tab("Configuration");
    add.group("Properties");
    add.text("name", "Name", [h]() -> std::string& { return h().name; }, "name");
    add.group("Locations");
    // A place kept as "none" until it is set: a box to set it, then its coordinates.
    auto place = [&add, &f](const std::string& key, const std::string& what, std::function<std::optional<JPMachineLocation>&()> at,
                            bool withZ) {
        add.row(what, at() ? Place::Location : Place::None);
        if (at()) {
            add.length(key + "X", what + " X", [at]() -> double& { return at()->x; });
            add.length(key + "Y", what + " Y", [at]() -> double& { return at()->y; });
            if (withZ) add.length(key + "Z", what + " Z", [at]() -> double& { return at()->z; });
            else add.skip();
        } else {
            add.skip();
            add.skip();
            add.skip();
        }
        add.flag(key, "Set?", [at] { return at().has_value(); },
                 [at](bool on) {
                     if (!on) at().reset();
                     else if (!at()) at() = JPMachineLocation();
                 });
        add.end();
        f.reshaping.push_back(key);
    };
    add.header({ "X", "Y", "Z", "Set?" });
    place("homingFiducial", "Homing Fiducial", [h]() -> std::optional<JPMachineLocation>& { return h().homingFiducial; }, true);
    if (h().homingFiducial) {
        add.length("homingFiducialDiameter", "Fiducial Diameter", [h]() -> double& { return h().homingFiducialDiameter; });
        add.row("Homing Method");
        add.choice("visualHoming", "Homing Method", { "Switches", "ResetToFiducialLocation" },
                   [h] { return std::string(h().visualHoming ? "ResetToFiducialLocation" : "Switches"); },
                   [h](const std::string& v) { h().visualHoming = v == "ResetToFiducialLocation"; });
        add.end();
        add.actions({ { "Visual Test", "visualTest" }, { "Visual Home", "visualHome" } });
        add.note("Set the homing fiducial up early, before capturing many places: each time it changes or moves, "
                 "every place captured since is off by as much.");
    }
    place("park", "Park Location", [h]() -> std::optional<JPMachineLocation>& { return h().park; }, false);

    add.group("Calibration Rig");
    add.header({ "X", "Y", "Z", "Set?" });
    place("rigPrimary", "Primary Mark", [h]() -> std::optional<JPMachineLocation>& { return h().rigPrimary; }, true);
    place("rigSecondary", "Secondary Mark", [h]() -> std::optional<JPMachineLocation>& { return h().rigSecondary; }, true);
    add.row("Mark Diameters");
    add.length("rigPrimaryDiameter", "Primary Diameter", [h]() -> double& { return h().rigPrimaryDiameter; });
    add.length("rigSecondaryDiameter", "Secondary Diameter", [h]() -> double& { return h().rigSecondaryDiameter; });
    add.length("rigTestObjectDiameter", "Test Object", [h]() -> double& { return h().rigTestObjectDiameter; });
    add.tip("The diameter of the test object the nozzles' precise offsets are calibrated with (a nozzle's Offset Wizard).");
    add.end();
    add.note("Two round marks at two heights. A head camera is calibrated over the homing fiducial and, with Two "
             "Heights? on (its Advanced Calibration), again over the secondary mark, at least 1 mm higher or lower.");

    add.group("Z Probe");
    add.byName("zProbeActuator", "Z Probe Actuator", named(cell.actuators, "(none)"), [h]() -> std::string& { return h().zProbeActuatorId; });
    add.tip("Read, in millimetres from where it is, wherever Capture Camera Location captures a place: the place's Z.");
    add.group("Pump");
    add.byName("pumpActuator", "Vacuum Pump Actuator", named(cell.actuators, "(none)"), [h]() -> std::string& { return h().pumpActuatorId; });
    add.choice("pumpControl", "Pump Control", { "None", "PartOn", "TaskDuration", "KeepRunning" },
               [h] { return h().pumpControl.empty() ? std::string("None") : h().pumpControl; },
               [h](const std::string& v) { h().pumpControl = v; });
    add.integer("pumpOnWaitMs", "Pump On Wait [ms]", [h]() -> int& { return h().pumpOnWaitMs; }, 0, 600000);
    add.note("PartOn: on while a nozzle holds a part. TaskDuration: on for the work, off with the last part. "
             "KeepRunning: once on, left on.");
}

void nozzleForm(JPCellConfig& cell, const std::string& id, JPSetupProperties::Form& f) {
    auto n = finder(cell.nozzles, id);
    f.title = "Nozzle " + n().name;
    JPFormBuilder add(f);
    add.tab("Configuration");
    add.group("Properties");
    add.text("name", "Name", [n]() -> std::string& { return n().name; }, "name");
    coordinateSystem<JPNozzleConfig>(add, cell, n, "(none)", false, f);
    add.group("Settings");
    add.integer("pickDwellMs", "Pick Dwell Time (ms)", [n]() -> int& { return n().pickDwellMs; }, 0, 60000);
    add.integer("placeDwellMs", "Place Dwell Time (ms)", [n]() -> int& { return n().placeDwellMs; }, 0, 60000);
    add.note("The total dwell is the nozzle's and its tip's together.");
    add.group("Safe Z");
    add.flag("dynamicSafeZ", "Dynamic Safe Z", [n]() -> bool& { return n().dynamicSafeZ; });
    add.tip("When moving to Safe Z, account for the part height on the nozzle i.e. lift the nozzle higher with a taller "
            "part. This allows you to use a lower Safe Z which might improve the machine speed.");
    add.note("Only for a nozzle on a Z axis of its own: on a shared Z (mapped, a cam) the other nozzle would go down.");
    add.group("Rotation");
    add.row("Rotation Mode");
    add.choice("rotationMode", "Rotation Mode", { "AbsolutePartAngle", "PlacementAngle", "MinimalRotation", "LimitedArticulation" },
               [n] { return n().rotationMode; }, [n](const std::string& v) { n().rotationMode = v; });
    add.flag("alignRotationWithPart", "Align with Part?", [n]() -> bool& { return n().alignRotationWithPart; });
    add.end();
    add.tip("After bottom vision part alignment, make the nozzle Rotation Mode offset align with the part rotation. "
            "This will make sure the nozzle coordinates as indicated in the reticle (cross-hairs), in the DRO etc. "
            "match the detected rotation of the part. After placing/discarding the part, the nozzle snaps back to "
            "indicating the original axis rotation.");
    f.reshaping.push_back("rotationMode");
    if (n().rotationMode == "LimitedArticulation") {
        add.number("maxPickArticulation", "Max. Pick Articulation", [n]() -> double& { return n().maxPickArticulation; }, 1);
        add.number("maxAlignArticulation", "Max. Alignment Articulation", [n]() -> double& { return n().maxAlignArticulation; }, 1);
    }
    add.note("How the nozzle turns for a part, as OpenPnP's: while it holds the part, its rotation reads the part's "
             "angle, its axis turned by the rotation mode offset. AbsolutePartAngle, no offset; PlacementAngle, the "
             "axis at 0 when the part is at its placement's angle; MinimalRotation, picked at whatever angle the axis "
             "has; LimitedArticulation, for a nozzle with a limited turn (its rotation axis limited to range, within "
             "its soft limits): about the middle of the range, room left for the pick and alignment corrections.");

    // Every tip: whether it fits this nozzle, and which one is on it now.
    add.tab("Nozzle Tips");
    add.group("Nozzle Tips");
    add.header({ "Compatible?", "Loaded?" });
    for (const JPNozzleTipConfig& t : cell.nozzleTips) {
        const std::string tid = t.id;
        add.row(t.name.empty() ? t.id : t.name);
        add.flag("fits:" + tid, "Compatible?", [n, tid] { return n().fits(tid); },
                 [n, tid](bool on) {
                     JPNozzleConfig& z = n();
                     std::erase(z.tipIds, tid);
                     if (on) z.tipIds.push_back(tid);
                     else if (z.tipId == tid) z.tipId.clear();   // a tip that does not fit is not on it
                 });
        add.flag("loaded:" + tid, "Loaded?", [n, tid] { return n().tipId == tid; },
                 [&cell, n, tid](bool on) {
                     JPNozzleConfig& z = n();
                     if (!on) {
                         if (z.tipId == tid) z.tipId.clear();
                         return;
                     }
                     // On this nozzle: it fits it, and it is on no other.
                     if (!z.fits(tid)) z.tipIds.push_back(tid);
                     for (JPNozzleConfig& other : cell.nozzles)
                         if (other.tipId == tid) other.tipId.clear();
                     z.tipId = tid;
                 });
        add.end();
    }
    if (cell.nozzleTips.empty()) add.note("No nozzle tips yet: add them under Nozzle Tips.");
    add.note("Loaded? says which tip is on the nozzle now; ticking it moves nothing.");

    add.tab("Vacuum");
    add.group("Vacuum");
    const JPFormBuilder::Named actuators = named(cell.actuators, "(none)");
    add.byName("vacuumActuator", "Vacuum Actuator", actuators, [n]() -> std::string& { return n().vacuumActuatorId; });
    add.row("Blow Off Actuator");
    add.byName("blowOffActuator", "Blow Off Actuator", actuators, [n]() -> std::string& { return n().blowOffActuatorId; });
    add.flag("blowOffClosesVacuum", "Closes Vacuum Actuator?", [n]() -> bool& { return n().blowOffClosesVacuum; });
    add.end();
    add.byName("vacuumSenseActuator", "Sensing Actuator", actuators, [n]() -> std::string& { return n().vacuumSenseActuatorId; });
    add.note("Pick switches the vacuum on; Place switches it off, then pulses the blow-off for the place dwell (Jog panel).");

    // Homing Z alone: after a tip forced on made the motor slip a step.
    // OpenPnP's ReferenceNozzleToolChangerWizard.
    add.tab("Tool Changer");
    add.group("Nozzle Tip Changer");
    add.flag("changerEnabled", "Automatic Tool Changer Enabled?", [n]() -> bool& { return n().changerEnabled; });
    add.tip("A tip change runs the tips' load and unload steps; off (or a tip without steps), it is asked to be done by hand.");
    add.flag("tipChangeOnManualPick", "Change On Manual Pick?", [n]() -> bool& { return n().tipChangeOnManualPick; });
    add.tip("A pick from the Feeders tab with a tip that does not fit the part changes to one that does.");
    add.header({ "X", "Y", "Z", "Rotation", "Set?" });
    auto manual = [n]() -> std::optional<JPMachineLocation>& { return n().manualChangeLocation; };
    add.row("Manual Change Location", manual() ? Place::Location : Place::None);
    if (manual()) {
        add.length("manualX", "Manual Change X", [manual]() -> double& { return manual()->x; });
        add.length("manualY", "Manual Change Y", [manual]() -> double& { return manual()->y; });
        add.length("manualZ", "Manual Change Z", [manual]() -> double& { return manual()->z; });
        add.number("manualRotation", "Manual Change Rotation", [manual]() -> double& { return manual()->rotation; });
    } else {
        for (int i = 0; i < 4; ++i) add.skip();
    }
    add.flag("manualSet", "Set?", [manual] { return manual().has_value(); },
             [manual](bool on) {
                 if (!on) manual().reset();
                 else if (!manual()) manual() = JPMachineLocation();
             });
    add.end();
    f.reshaping.push_back("manualSet");
    add.note("Where the nozzle goes, by way of safe Z, for its tip to be changed by hand; not set, it stays where it is.");

    add.tab("Homing");
    add.group("Z Home");
    add.text("homeCommand", "Home Command", [n]() -> std::string& { return n().homeCommand; }, "lines");
    add.note("G-code that homes this nozzle's Z alone, sent to the controller of its Z motor once the head is at "
             "its park place (which must be clear of anything below). After it, the Z is at its home coordinate, "
             "as after Home. A nozzle sharing the motor is homed with it. Empty: no Z home.");
    add.actions({ { "Home Z", "homeNozzleZ" } });

    // OpenPnP's Offset Wizard: where the nozzle is on the head, from a mark it leaves.
    add.tab("Offset Wizard");
    add.group("Nozzle Offset Wizard Steps");
    add.note("1. Put something on the table the nozzle can leave a mark in (putty, flour, carbon paper).");
    add.note("2. Choose this nozzle on the Jog panel, move it over the object and lower it until it leaves a mark; "
             "turning it a full turn there keeps the tip's runout out of the measurement.");
    add.note("3. Store the nozzle mark position (where the nozzle is now):");
    add.actions({ { "Store Nozzle Mark Position", "storeNozzleMark" } });
    add.note("4. Raise the nozzle, choose the camera on the Jog panel and move it over the centre of the mark, then:");
    add.actions({ { "Calculate Nozzle Offset", "calculateNozzleOffset" } });
    add.note("The offsets on the Configuration tab change by how far the camera is from where the nozzle thought it "
             "was; Undo takes them back.");
    // OpenPnP's precise camera <-> nozzle offsets calibration with a test object.
    add.group("Precise Offsets with a Test Object");
    add.note("Place the calibration test object (its diameter: the head's Calibration Rig Test Object) onto the head's "
             "primary calibration fiducial, load the right nozzle tip and ready the vacuum. The camera finds the "
             "object; the nozzle picks it at six angles round the circle and places it turned 180 degrees, the camera "
             "finding it after each; the true nozzle axis is midway, so its X and Y offsets change by the average of "
             "where it moved (runout cancels out). CAUTION: the nozzle moves to the test object.");
    add.actions({ { "Calibrate Precise Offsets", "calibrateNozzleOffsets" } });

    // OpenPnP's ContactProbeNozzle wizard.
    add.tab("Contact Probe");
    add.group("Contact Probing");
    auto cp = [n]() -> JPNozzleConfig::ContactProbe& { return n().contactProbe; };
    add.choice("contactProbeMethod", "Method", { "None", "VacuumSense", "ContactSenseActuator" }, [cp] { return cp().method; },
               [cp](const std::string& v) { cp().method = v; });
    f.reshaping.push_back("contactProbeMethod");
    if (cp().method == "ContactSenseActuator")
        add.byName("contactProbeActuator", "Contact Sense Actuator", named(cell.actuators, "(none)"),
                   [cp]() -> std::string& { return cp().actuatorId; });
    add.number("contactProbeSpeed", "Probe Speed", [cp]() -> double& { return cp().speed; });
    add.tip("Probing speed factor, for the contact sense actuator's probing command.");
    add.length("contactProbeStartOffset", "Start Offset", [cp]() -> double& { return cp().startOffsetMm; });
    add.tip("Contact probing start offset in Z above the nominal location. Note: for part height probing, the maximum "
            "part height on the NozzleTip is used instead, if the part height is not yet known.");
    add.length("contactProbeDepth", "Probe Depth", [cp]() -> double& { return cp().depthMm; });
    add.tip("Maximum contact probing depth in Z, from the Start Offset.");
    if (cp().method == "VacuumSense") {
        add.length("sniffleIncrement", "Sniffle Increment", [cp]() -> double& { return cp().sniffleIncrementMm; });
        add.tip("Vacuum sensing \"sniffle\" increment in Z.");
        add.integer("sniffleDwellTime", "Sniffle Dwell Time [ms]", [cp]() -> int& { return cp().sniffleDwellMs; }, 0, 60000);
    }
    add.length("contactProbeAdjust", "Final Adjustment", [cp]() -> double& { return cp().adjustMm; });
    add.tip("Contact probing final adjustment in Z (positive values point upwards in Z). Use positive values to "
            "compensate probing overshoot; negative values to add additional nozzle tip spring tensioning.");
    const Strings triggers{ "Off", "Once", "AfterHoming", "EachTime" };
    add.choice("feederHeightProbing", "Feeder Height Probing", triggers, [cp] { return cp().feederHeightProbing; },
               [cp](const std::string& v) { cp().feederHeightProbing = v; });
    add.tip("Probe for feeder heights. On some feeder types, this can probe for the Part Height, when it is unknown.");
    add.choice("partHeightProbing", "Placement Height Probing", triggers, [cp] { return cp().partHeightProbing; },
               [cp](const std::string& v) { cp().partHeightProbing = v; });
    add.tip("Probe for placement heights. Includes probing for Part Height, when it is unknown.");
    add.flag("discardProbing", "Discard Probing", [cp]() -> bool& { return cp().discardProbing; });
    add.tip("Enable contact probing for discard. There must be a surface that the nozzle can probe into that is likely "
            "to brush/tilt off a part from the nozzle, like a (ESD safe) soft material or a slanted surface.");
    add.note("ContactSenseActuator: the actuator switched on is the controller's probing move down until contact (e.g. "
             "G38.2), switched off its retract; the nozzle's Z is then where the controller says it stopped. "
             "VacuumSense: the nozzle stepped down a Sniffle Increment at a time until its tip's part-off check finds "
             "the nozzle blocked. Probing heights are kept by feeder and by part (while jplacer runs): Once until it "
             "is closed, AfterHoming until the machine is homed, EachTime never.");
}

void nozzleTipForm(JPCellConfig& cell, const std::string& id, JPSetupProperties::Form& f,
                   const JPSetupProperties::Live& live) {
    auto t = finder(cell.nozzleTips, id);
    f.title = "Nozzle tip " + t().name;
    JPFormBuilder add(f);
    add.tab("Configuration");
    add.group("Properties");
    add.text("name", "Name", [t]() -> std::string& { return t().name; }, "name");
    add.group("Pick & Place");
    add.integer("pickDwellMs", "Pick Dwell Time (ms)", [t]() -> int& { return t().pickDwellMs; }, 0, 60000);
    add.integer("placeDwellMs", "Place Dwell Time (ms)", [t]() -> int& { return t().placeDwellMs; }, 0, 60000);
    add.number("placeBlowOffLevel", "Place Blow-Off Level", [t]() -> double& { return t().placeBlowOffLevel; }, 3);
    add.tip("Default placement blow-off level, if none is given on the Package.");
    add.note("The dwell times are added to the nozzle's own. The blow-off at place: the part's package's level, else this; "
             "0, no blow-off.");
    add.group("Push and Drag Usage");
    add.flag("pushAndDragAllowed", "Push & Drag allowed?", [t]() -> bool& { return t().pushAndDragAllowed; });
    add.tip("Determines if the NozzleTip is allowed to be used for pushing and dragging.\nShould only be enabled for NozzleTips "
            "that are sturdy enough to take the lateral forces, including the occasional snag.");
    add.length("diameterLowMm", "Outside Diameter", [t]() -> double& { return t().diameterLowMm; });
    add.tip("Outside diameter of the nozzle tip at the lowest ~0.75mm.");
    add.group("Part Dimensions");
    add.length("diameter", "Diameter Seen From Below", [t]() -> double& { return t().diameter; });
    add.length("minPartDiameterMm", "Min. Part Diameter", [t]() -> double& { return t().minPartDiameterMm; });
    add.tip("Minimum part diameter, to be picked with this the nozzle tip: at least the tip's air bore plus two times the "
            "Max. Pick Tolerance.");
    add.length("maxPartDiameterMm", "Max. Part Diameter", [t]() -> double& { return t().maxPartDiameterMm; });
    add.tip("Maximum diameter/diagonal of parts picked with this nozzle tip, including tolerances.");

    add.length("maxPartHeightMm", "Max. Part Height", [t]() -> double& { return t().maxPartHeightMm; });
    add.tip("Maximum part heights picked with this nozzle tip. Used for dynamic safe Z, if part height is unknown.");
    add.length("maxPickToleranceMm", "Max. Pick Tolerance", [t]() -> double& { return t().maxPickToleranceMm; });
    add.tip("Maximum assumed pick tolerance allowed with this nozzle tip.\nThis determines how far away from the nominal "
            "location a detected Bottom Vision alignment position is accepted. It also reduces the computation time of some "
            "vision operations by limiting the search range.");
    add.group("Nozzles");
    for (const JPNozzleConfig& n : cell.nozzles) {
        auto nozzle = finder(cell.nozzles, n.id);
        add.flag("fits:" + n.id, n.name.empty() ? n.id : n.name, [nozzle, id] { return nozzle().fits(id); },
                 [nozzle, id](bool on) {
                     JPNozzleConfig& z = nozzle();
                     std::erase(z.tipIds, id);
                     if (on) z.tipIds.push_back(id);
                     else if (z.tipId == id) z.tipId.clear();   // a tip that does not fit is not on it
                 });
    }
    add.note("The nozzles this tip fits.");

    add.tab("Part Detection");
    for (const auto& [title, on] : { std::pair{ "Part On Vacuum Sensing", true }, std::pair{ "Part Off Vacuum Sensing", false } }) {
        add.group(title);
        auto sensing = [t, on = on]() -> JPNozzleTipConfig::Sensing& { return on ? t().partOn : t().partOff; };
        const std::string key = on ? "partOn" : "partOff";
        add.row("Measurement Method");
        add.choice(key + "Method", "Measurement Method", { "None", "Absolute", "Difference" },
                   [sensing] { return sensing().method; }, [sensing](const std::string& v) { sensing().method = v; });
        f.reshaping.push_back(key + "Method");
        if (sensing().method != "None") {
            add.flag(key + "Establish", "Establish Level?", [sensing]() -> bool& { return sensing().establish; });
            add.tip(on ? "While the nozzle is pressed down on the part in the pick operation, the vacuum level is repeatedly measured "
                         "until it builds up to the Vacuum Range or the Pick Dwell Time timeout expires, whichever comes first."
                       : "While the nozzle is pressed down on the part in the place operation, the vacuum level is repeatedly "
                         "measured until it decays to the Vacuum Range or the Place Dwell Time timeout expires, whichever comes first.");
            f.reshaping.push_back(key + "Establish");
        }
        add.end();
        const JPNozzleTipConfig::Sensing& now = sensing();
        if (now.method == "None") continue;
        add.row("Perform Checks?");
        add.skip();
        if (on) {
            add.flag("partOnCheckAfterPick", "After Pick", [t]() -> bool& { return t().partOnCheckAfterPick; });
            add.flag("partOnCheckAlign", "Alignment", [t]() -> bool& { return t().partOnCheckAlign; });
            add.flag("partOnCheckBeforePlace", "Before Place", [t]() -> bool& { return t().partOnCheckBeforePlace; });
        } else {
            add.flag("partOffCheckAfterPlace", "After Place", [t]() -> bool& { return t().partOffCheckAfterPlace; });
            add.flag("partOffCheckBeforePick", "Before Pick", [t]() -> bool& { return t().partOffCheckBeforePick; });
        }
        add.end();
        if (!on) {
            add.row("Valve open/close (ms)");
            add.integer("partOffProbingMs", "Valve open (ms)", [t]() -> int& { return t().partOffProbingMs; }, 0, 60000);
            add.tip("The valve is opened and closed to create a small underpressure pulse. The open time should be quite short, no "
                    "point in creating full pick suction. The close time can be used to wait for the system to react to the "
                    "pulse, including delays in sensor signal propagation and readout.");
            add.integer("partOffDwellMs", "Valve close (ms)", [t]() -> int& { return t().partOffDwellMs; }, 0, 60000);
            add.end();
        }
        auto reading = [](const std::optional<double>& v) {
            char text[32] = "";
            if (v) std::snprintf(text, sizeof text, "%.1f", *v);
            return std::string(text);
        };
        add.header({ "Low Value", "High Value", "Last Reading" });
        add.row("Vacuum Range");
        add.number(key + "Low", "Vacuum Low", [sensing]() -> double& { return sensing().low; }, 1);
        add.number(key + "High", "Vacuum High", [sensing]() -> double& { return sensing().high; }, 1);
        add.words(reading(now.lastReading));
        add.end();
        if (now.method == "Difference") {
            add.row("Difference Range");
            add.number(key + "DiffLow", "Difference Low", [sensing]() -> double& { return sensing().diffLow; }, 1);
            add.number(key + "DiffHigh", "Difference High", [sensing]() -> double& { return sensing().diffHigh; }, 1);
            add.words(reading(now.lastDifference));
            add.end();
        }
        add.endColumns();
        // OpenPnP's graph of the last pick (place) and check: the vacuum, and the valve below it.
        if ((now.method == "Difference" || now.establish) && !now.vacuumGraph.empty()) {
            // OpenPnP's two scales: the vacuum's, and the valve's below it.
            auto plot = std::make_shared<JPPlot>();
            plot->xTitle = "ms";
            plot->yTitle = "Vacuum";
            plot->y2Title = "Valve";
            plot->y2Lo = -kValveBandBelow;
            plot->y2Hi = 1 + kValveBandAbove;
            JPPlot::Series vacuum { "Vacuum", JPPlot::Tone::First, {} }, valve { "Valve", JPPlot::Tone::Second, {}, true };
            for (const auto& [ms, level] : now.vacuumGraph) vacuum.points.push_back({ ms, level });
            for (const auto& [ms, open] : now.valveGraph) valve.points.push_back({ ms, open });
            plot->series = { vacuum, valve };
            add.plot(on ? "Last Pick" : "Last Place or Check", plot);
        }
    }

    add.tab("Tool Changer");
    add.group("Nozzle Tip Changer");
    // OpenPnP's: four locations, the speed between each two, an actuator
    // switched after each of the first three; over the loading steps when
    // they are in its form (made here, or brought in from OpenPnP).
    if (const auto form = t().openPnpChanger()) {
        using C = JPNozzleTipConfig::OpenPnpChanger;
        auto change = [t](const std::function<void(C&)>& edit) {
            C c = t().openPnpChanger().value_or(C {});
            edit(c);
            t().setOpenPnpChanger(c);
        };
        auto now = [t] { return t().openPnpChanger().value_or(C {}); };
        JPFormBuilder::Named actuators;
        actuators.add("", "");
        for (const JPActuatorConfig& a : cell.actuators) actuators.add(a.name.empty() ? a.id : a.name, a.id);
        static const char* const kLocations[] = { "First Location", "Second Location", "Third Location", "Last Location" };
        static const char* const kSpeeds[] = { "1 \xE2\x86\x94 2", "2 \xE2\x86\x94 3", "3 \xE2\x86\x94 4" };
        add.header({ "X", "Y", "Z", "Rotation", "Speed", "Set?" });
        for (size_t k = 0; k < 4; ++k) {
            const std::string n = std::to_string(k + 1);
            add.row(kLocations[k], form->at[k] ? Place::Location : Place::None);
            add.positionNoSafeZ();
            if (form->at[k]) {
                auto coordinate = [&](const char* key, const char* label, double JPMachineLocation::*field, bool rotation) {
                    add.coordinate(rotation, std::string("changer") + key + n, label,
                                   [now, k, field] { return now().at[k] ? (*now().at[k]).*field : 0.0; },
                                   [change, k, field](double v) { change([&](C& c) { if (c.at[k]) (*c.at[k]).*field = v; }); });
                };
                coordinate("X", "X", &JPMachineLocation::x, false);
                coordinate("Y", "Y", &JPMachineLocation::y, false);
                coordinate("Z", "Z", &JPMachineLocation::z, false);
                coordinate("Rotation", "Rotation", &JPMachineLocation::rotation, true);
            } else {
                for (int i = 0; i < 4; ++i) add.skip();
            }
            add.skip();   // the speeds are between the locations
            add.flag("changerSet" + n, "Set?", [now, k] { return now().at[k].has_value(); },
                     [change, k](bool on) {
                         change([&](C& c) {
                             if (!on) c.at[k].reset();
                             else if (!c.at[k]) c.at[k] = JPMachineLocation();
                         });
                     });
            add.end();
            f.reshaping.push_back("changerSet" + n);
            if (k == 3) break;
            add.row(std::string("Post ") + n + " Actuator");
            add.byName("changerPost" + n, std::string("Post ") + n + " Actuator", actuators, [now, k] { return now().post[k]; },
                       [change, k](const std::string& id) { change([&](C& c) { c.post[k] = id; }); });
            add.skip();
            add.words(kSpeeds[k]);
            add.skip();
            add.number("changerSpeed" + n, kSpeeds[k], [now, k] { return now().speed[k + 1]; },
                       [change, k](double v) { change([&](C& c) { c.speed[k + 1] = std::clamp(v, 0.0, 1.0); }); });
            add.end();
            add.tip(std::string("Speed between ") + kLocations[k] + " and " + kLocations[k + 1] + " (a share of the machine's).");
        }
        add.endColumns();
        add.note("Loading goes to the First Location by way of Safe Z, then to each set location in turn at the speed "
                 "between, switching each Post Actuator on after its location; unloading goes back the same way, "
                 "switching them off. The steps are also in the tree under the tip.");
    } else {
        add.note("This tip's loading steps are jplacer's own (in the tree under the tip): OpenPnP's four locations "
                 "show a tip's steps made with them, or brought in from OpenPnP.");
    }
    add.choice("unloading", "Unloading", { "loading backwards", "steps of its own" },
               [t] { return std::string(t().unloadReversesLoad ? "loading backwards" : "steps of its own"); },
               [t](const std::string& v) {
                   JPNozzleTipConfig& tip = t();
                   const bool backwards = v == "loading backwards";
                   // Its own start as loading backwards, to change from there.
                   if (!backwards && tip.unloadReversesLoad && tip.unloadSteps.empty())
                       tip.unloadSteps = tip.unloadingSteps();
                   tip.unloadReversesLoad = backwards;
               });
    add.note("The steps of loading and unloading are in the tree under the tip: select one to change it, "
             "Add to add one after it.");
    // OpenPnP's Vision Calibration of the changer slot (JPNozzleTipConfig::VisionCalibration).
    add.group("Vision Calibration");
    auto vc = [t]() -> JPNozzleTipConfig::VisionCalibration& { return t().visionCalibration; };
    add.row("Vision Location");
    add.choice("visionLocation", "Vision Location",
               { "None", "FirstLocation", "SecondLocation", "ThirdLocation", "LastLocation", "TouchLocation" },
               [vc] { return vc().location; }, [vc](const std::string& v) { vc().location = v; });
    add.tip("Location for vision calibration, or None for no calibration. Choose a location where the nozzle tip in the slot is "
            "visible.");
    if (vc().on()) {
        add.length("visionZAdjust", "Adjust Z", [vc]() -> double& { return vc().zAdjustMm; });
        add.tip("Adjust the Z coordinate of the Vision Location by this offset. Set it to the Z distance between the template "
                "subject that you are detecting with Vision Calibration, e.g. the visible surface of the changer slot, and the "
                "(imaginary) underside of the nozzle tip when at that location. Positive adjustment when the nozzle tip is below, "
                "negative when it is above that surface. The setting can overcome scaling errors in the camera view, especially "
                "when things are much closer to the camera than usual. 3D Units per Pixel must be configured on the camera.");
    }
    add.end();
    f.reshaping.push_back("visionLocation");
    if (vc().on()) {
        add.note("Capture two template images of your nozzle tip changer slot both in empty and occupied state. Using the "
                 "templates, vision calibration will then calibrate the changer locations in X/Y and also make sure the slot is "
                 "empty or occupied as expected.");
        add.choice("visionTrigger", "Calibration Trigger", { "Manual", "MachineHome", "NozzleTipChange" },
                   [vc] { return vc().trigger; }, [vc](const std::string& v) { vc().trigger = v; });
        static const char* const kTemplateTip =
            "The template is centered around the selected Vision Location. Choose dimensions as small as possible, but the "
            "templates should include tell-tale horizontal and vertical edges. Furthermore, the nozzle tip should be visible "
            "when it occupies the changer slot.";
        add.row("Template Width");
        add.length("visionTemplateWidth", "Template Width", [vc]() -> double& { return vc().templateWidthMm; });
        add.tip(std::string("Template image width (X). ") + kTemplateTip);
        add.length("visionTemplateHeight", "Template Height", [vc]() -> double& { return vc().templateHeightMm; });
        add.tip(std::string("Template image height (Y). ") + kTemplateTip);
        add.end();
        add.row("Tolerance");
        add.length("visionTolerance", "Tolerance", [vc]() -> double& { return vc().toleranceMm; });
        add.tip("Maximum calibration tolerance i.e. how far away from the nominal location the calibrated location can be.");
        add.length("visionPrecision", "Wanted Precision", [vc]() -> double& { return vc().precisionMm; });
        add.tip("If the detected template image match is further away than the Wanted Precision, the camera is re-centered and "
                "another vision pass is made.");
        add.end();
        add.integer("visionMaxPasses", "Max. Passes", [vc]() -> int& { return vc().maxPasses; }, 1, 100);
        add.row("Minimum Score");
        add.number("visionMinimumScore", "Minimum Score", [vc]() -> double& { return vc().minimumScore; });
        add.tip("When the template images are matched against the camera image, a score is computed indicating the quality of "
                "the match. If the obtained score is smaller than the Minimum Score given here, the calibration fails. This "
                "should stop the machine from atempting a nozzle tip change, when the position is wrong.");
        add.words("Last Score");
        char score[32] = "";
        if (vc().lastScore) std::snprintf(score, sizeof score, "%.3f", *vc().lastScore);
        add.words(score);
        add.button("testSlotVision", "Test", "Test the vision calibration.");
        add.end();
        for (const bool empty : { true, false }) {
            const std::string what = empty ? "Empty" : "Occupied";
            add.row("Template " + what);
            add.button(empty ? "captureSlotEmpty" : "captureSlotOccupied", "Capture",
                       "Capture the template image for the " + std::string(empty ? "empty" : "occupied") + " nozzle tip holder slot.");
            add.button(empty ? "resetSlotEmpty" : "resetSlotOccupied", "Reset",
                       "Reset the template image for the " + std::string(empty ? "empty" : "occupied") + " nozzle tip holder slot.");
            add.end();
            const std::string file = empty ? vc().templateEmpty : vc().templateOccupied;
            if (!file.empty() && live.templatePicture)
                add.image("", [picture = live.templatePicture, file] { return picture(file); });
        }
    }

    // OpenPnP's Cloning Settings.
    add.group("Cloning Settings");
    add.choice("cloning", "Behavior", { "Template", "Clones from Template", "Locked" },
               [t] { return std::string(t().templateTip ? "Template" : t().templateLocked ? "Locked" : "Clones from Template"); },
               [t, &cell](const std::string& v) {
                   const std::string id = t().id;
                   // One template only.
                   if (v == "Template")
                       for (JPNozzleTipConfig& other : cell.nozzleTips) other.templateTip = false;
                   for (JPNozzleTipConfig& tip : cell.nozzleTips)
                       if (tip.id == id) {
                           tip.templateTip = v == "Template";
                           tip.templateLocked = v == "Locked";
                       }
               });
    add.tip("One nozzle tip can become the Template for others to be cloned from. Locations are translated relative "
            "to the First Location (each tip's first move). If individual nozzle tips are special, mark them as "
            "Locked to prevent cloning.");
    f.reshaping.push_back("cloning");
    const JPNozzleTipConfig* templ = nullptr;
    for (const JPNozzleTipConfig& tip : cell.nozzleTips)
        if (tip.templateTip) templ = &tip;
    const bool zProbing = std::any_of(cell.nozzles.begin(), cell.nozzles.end(), [](const JPNozzleConfig& n) { return n.contactProbe.on(); });
    // What a clone takes: chosen for the session, all to begin with, as OpenPnP's form opens.
    static JPNozzleTipConfig::ClonedParts parts;
    if (t().templateTip) {
        add.editButton("cloneToAll", "Clone Tool Changer Settings to all Nozzle Tips", "Clone Tool Changer Settings to all",
                       [&cell, id] {
                           const JPNozzleTipConfig* from = nullptr;
                           for (const JPNozzleTipConfig& tip : cell.nozzleTips)
                               if (tip.id == id) from = &tip;
                           if (!from) return;
                           const JPNozzleTipConfig source = *from;
                           for (JPNozzleTipConfig& tip : cell.nozzleTips)
                               if (tip.id != id) tip.cloneChangerFrom(source, parts);
                       });
        add.tip("Clone the Tool Changer settings from this Template nozzle tip, to all the others. Locations are "
                "translated relative to First Locations.");
    } else {
        const std::string templateId = templ ? templ->id : std::string();
        add.editButton("cloneFromTemplate", "Clone Tool Changer Settings from Template", "Clone Tool Changer Settings from Template",
                       [t, &cell, templateId] {
                           for (const JPNozzleTipConfig& tip : cell.nozzleTips)
                               if (tip.id == templateId) {
                                   const JPNozzleTipConfig source = tip;
                                   t().cloneChangerFrom(source, parts);
                               }
                       },
                       // Not for a locked tip, nor with no template to clone from.
                       !t().templateLocked && templ);
        add.tip("Clone the tool changer settings from the nozzle tip marked as Template. All the locations are "
                "translated relative to First Location.");
    }
    add.row("Locations?");
    add.flag("cloneLocations", "Locations?", [] { return parts.locations; }, [](bool on) { parts.locations = on; });
    if (zProbing)
        add.flag("cloneZCalibration", "Z Calibration?", [] { return parts.zCalibration; }, [](bool on) { parts.zCalibration = on; });
    add.flag("cloneVisionCalibration", "Vision Calibration?", [] { return parts.visionCalibration; },
             [](bool on) { parts.visionCalibration = on; });
    add.end();
    for (const char* choice : { "cloneLocations", "cloneZCalibration", "cloneVisionCalibration" }) f.viewOnly.push_back(choice);
    add.button("referenceTouchZ", "Calibrate all Touch Locations' Z to Template",
               "Calibrate all the nozzle tip's touch location Z to the Template reference. This will load the template "
               "nozzle tip on the default probing nozzle, recalibrate the template's touch location Z and then probe and "
               "reference all the others to it. Note, unlike cloning this does include nozzle tips marked as Locked.",
               t().templateTip);
    add.note("Cloning needs a first move in this tip's loading steps and in the template's: it is where each is "
             "taken from.");
    // OpenPnP's Z calibration by touch: shown where a nozzle probes by contact.
    if (zProbing) {
        add.group("Z Calibration");
        add.header({ "X", "Y", "Z", "Rotation", "Set?" });
        auto touch = [t]() -> std::optional<JPMachineLocation>& { return t().touchLocation; };
        add.row("Touch Location", touch() ? Place::Location : Place::None);
        add.contactProbe();
        if (touch()) {
            add.length("touchX", "Touch X", [touch]() -> double& { return touch()->x; });
            add.length("touchY", "Touch Y", [touch]() -> double& { return touch()->y; });
            add.length("touchZ", "Touch Z", [touch]() -> double& { return touch()->z; });
            add.number("touchRotation", "Touch Rotation", [touch]() -> double& { return touch()->rotation; });
        } else {
            for (int i = 0; i < 4; ++i) add.skip();
        }
        add.flag("touchSet", "Set?", [touch] { return touch().has_value(); },
                 [touch](bool on) {
                     if (!on) touch().reset();
                     else if (!touch()) touch() = JPMachineLocation();
                 });
        add.end();
        f.reshaping.push_back("touchSet");
        add.endColumns();
        add.choice("zCalibrationTrigger", "Z Calibration", { "Manual", "MachineHome", "NozzleTipChange" },
                   [t] { return t().zCalibrationTrigger; }, [t](const std::string& v) { t().zCalibrationTrigger = v; });
        add.tip("When the tip's Z is calibrated by touch: Manual (Calibrate Now only), MachineHome (once the machine is "
                "homed), NozzleTipChange (once homed, and each time it is loaded).");
        add.flag("zCalibrationFailHoming", "Fail Homing?", [t]() -> bool& { return t().zCalibrationFailHoming; });
        add.tip("A calibration failing once the machine is homed fails the homing.");
        add.actions({ { "Calibrate Now", "calibrateZ" }, { "Reset", "resetZCalibration" } });
        add.note("The nozzle the tip is on probes the Touch Location from its Start Offset (Contact Probe tab); how far "
                 "it met it from Z, up to the nozzle's largest Z offset, moves every Z of that nozzle, while the tip "
                 "stays on it.");
    }

    add.tab("Calibration");
    add.group("Runout");
    auto rc = [t]() -> JPNozzleTipConfig::RunoutCalibration& { return t().runoutCalibration; };
    using RC = JPNozzleTipConfig::RunoutCalibration;
    add.flag("runoutEnabled", "Compensate?", [rc]() -> bool& { return rc().enabled; });
    add.choice("runoutRecalibration", "Auto Recalibration", { "NozzleTipChange", "NozzleTipChangeInJob", "MachineHome", "Manual" },
               [rc] { return rc().recalibration; }, [rc](const std::string& v) { rc().recalibration = v; });
    add.tip("Determines when a recalibration is automatically executed: on each nozzle tip change; on each nozzle tip "
            "change but only in Jobs; on each machine homing (and on nozzle tip change when not yet calibrated); or "
            "manually only.");
    add.flag("runoutFailHoming", "Fail Homing?", [rc]() -> bool& { return rc().failHoming; });
    add.tip("When the calibration fails during homing, also fail the homing cycle.");
    add.integer("runoutDivisions", "Circle Divisions", [rc]() -> int& { return rc().divisions; }, RC::kLeastDivisions,
                RC::kMostDivisions);
    add.integer("runoutMisdetects", "Allowed Misdetects", [rc]() -> int& { return rc().misdetects; }, 0, RC::kMostDivisions);
    add.length("runoutOffsetThreshold", "Offset Threshold", [rc] { return rc().offsetThresholdMm; },
               [rc](double v) { if (v > 0) rc().offsetThresholdMm = v; });
    add.tip("The largest runout (and nozzle offset error) accepted: a tip found further than this from where the nozzle "
            "was sent counts as a misdetect.");
    add.length("runoutZOffset", "Calibration Z Offset", [rc]() -> double& { return rc().zOffset; });
    add.length("runoutVisionDiameter", "Vision Diameter", [rc] { return rc().visionDiameter; },
               [rc](double v) { if (v >= 0) rc().visionDiameter = v; });
    add.actions({ { "Position Tool", "positionRunoutTool" }, { "Calibrate", "calibrateRunout" }, { "Reset", "resetRunout" } });
    add.note("Position Tool takes the nozzle the tip is on over the camera looking up, at its focus plus the Z offset. "
             "Calibrate measures the tip on the nozzle it is on, over the fixed camera looking up: down to the "
             "camera's focus (plus the Z offset), turned to each of Circle Divisions angles round the circle, its "
             "end found at each (Vision Diameter across; 0: the tip's diameter), and a circle fitted. With Compensate? "
             "on, every move of that nozzle is sent the swing the other way, so the tip's centre lands where it is "
             "sent at any angle. Reset forgets it for that nozzle.");
    for (const auto& [nozzleId, r] : t().runout) {
        std::string nozzleName = nozzleId;
        for (const JPNozzleConfig& n : cell.nozzles)
            if (n.id == nozzleId) nozzleName = n.name;
        add.group("On " + nozzleName + ", " + r.when);
        const std::string key = "runout." + nozzleId + ".";
        auto shown = [&add, &key](const std::string& name, const std::string& label, const std::string& value) {
            add.text(key + name, label, [value] { return value; }, nullptr);
        };
        char b[120];
        std::snprintf(b, sizeof b, "%.4f mm at %.1f deg", r.radius, r.phaseDeg);
        shown("runout", "Runout", b);
        std::snprintf(b, sizeof b, "%+.4f, %+.4f mm", r.centreX, r.centreY);
        shown("axis", "Axis Off By", b);
        std::snprintf(b, sizeof b, "%.4f mm, worst %.4f", r.rmsMm, r.peakMm);
        shown("fit", "Fit", b);
        add.note("Axis Off By: how far the nozzle's axis is from where the camera's position and the nozzle's offset "
                 "say; one of them is off by that much.");
        auto plot = std::make_shared<JPPlot>();
        plot->kind = JPPlot::Kind::Scatter;
        plot->xTitle = "X mm";
        plot->yTitle = "Y mm";
        plot->circle = r.radius;
        JPPlot::Series seen{ "measured", JPPlot::Tone::First, {} };
        for (const JPRunout::Point& p : r.points) seen.points.push_back({ p.dx - r.centreX, p.dy - r.centreY });
        plot->series.push_back(seen);
        add.plot("Where Its End Was, About Its Axis", plot);
        add.note("Each angle's measurement about the fitted axis, on the fitted circle: points well off it mean the "
                 "end was found badly, or the nozzle wobbles.");
    }
    // OpenPnP's Background Calibration, measured along with the runout.
    add.group("Background Calibration");
    auto bg = [t]() -> JPNozzleTipConfig::Background& { return t().background; };
    add.choice("backgroundMethod", "Method", { "None", "Brightness", "BrightnessAndKeyColor" }, [bg] { return bg().method; },
               [bg](const std::string& v) { bg().method = v; });
    add.length("minimumDetailSize", "Minimum Detail Size", [bg] { return bg().minimumDetailSizeMm; },
               [bg](double v) { if (v > 0) bg().minimumDetailSizeMm = v; });
    add.tip("Specify the size of the smallest details in the image that are considered a meaningfull part of the shape "
            "to be detected, like the smallest contacts etc. Smaller specks and artifacts, like dust, scratches, "
            "texture, etc. are blurred out.");
    add.header({ "Minimum", "Maximum", "Tolerance" });
    auto channel = [&add, bg](const std::string& key, const std::string& label, const std::string& tip, int JPNozzleTipConfig::Background::*lo,
                              int JPNozzleTipConfig::Background::*hi, int JPNozzleTipConfig::Background::*tol) {
        add.row(label);
        add.text(key + "Min", label + " Minimum", [bg, lo] { return std::to_string(bg().*lo); }, nullptr);
        add.text(key + "Max", label + " Maximum", [bg, hi] { return std::to_string(bg().*hi); }, nullptr);
        add.integer(key + "Tol", label + " Tolerance", [bg, tol]() -> int& { return bg().*tol; }, 0, 255);
        add.end();
        add.tip(tip);
    };
    using B = JPNozzleTipConfig::Background;
    channel("backgroundHue", "Hue", "Base Color, Hue in the HSV color model", &B::minHue, &B::maxHue, &B::tolHue);
    channel("backgroundSaturation", "Saturation", "Saturation in the HSV color model", &B::minSaturation, &B::maxSaturation,
            &B::tolSaturation);
    channel("backgroundValue", "Value", "Brightness, Value in the HSV color model", &B::minValue, &B::maxValue, &B::tolValue);
    add.endColumns();
    add.note(bg().diagnostics.empty() ? std::string("No diagnostics yet.") : bg().diagnostics);
    add.actions({ { "Show Problems", "showBackgroundProblems" } });
    add.note("Calibrate (Runout, above) measures the background too: the pictures of the tip all round give the "
             "background's range, which bottom vision masks (each widened by its Tolerance) for parts on this tip. "
             "Show Problems shows, on the camera looking up, the pictures with background the mask would not take, "
             "beside the same with it marked.");
}

// An optional coordinate as text: empty when left out (the nozzle stays as it
// is on that axis). What is not a number is not taken.
void coordinate(JPFormBuilder& add, const std::string& name, const std::string& label,
                std::function<std::optional<double>&()> ref) {
    add.text(name, label, [ref] { return ref() ? JPSetupTree::shortNumber(*ref()) : std::string(); },
             [ref](const std::string& v) {
                 if (v.find_first_not_of(" \t") == std::string::npos) {
                     ref().reset();
                     return;
                 }
                 char* end = nullptr;
                 const double d = std::strtod(v.c_str(), &end);
                 if (end && end != v.c_str() && v.find_first_not_of(" \t", size_t(end - v.c_str())) == std::string::npos)
                     ref() = d;
             }, "number");
}

void stepForm(JPCellConfig& cell, const JPSetupTree::Path& p, JPSetupProperties::Form& f) {
    using S = JPChangerStep;
    auto tip = finder(cell.nozzleTips, p.owner);
    const size_t index = size_t(std::strtoul(p.id.c_str(), nullptr, 10));
    const bool load = p.list == "load";
    f.title = "Nozzle tip " + tip().name + ": " + (load ? "load" : "unload") + " step " + std::to_string(index + 1);
    JPFormBuilder add(f);
    add.tab("Step");
    add.group("Step");
    if (!load && tip().unloadReversesLoad) {
        // Worked out from loading; changed by changing loading.
        const std::vector<S> steps = tip().unloadingSteps();
        if (index >= steps.size()) return;
        const std::string what = JPSetupTree::stepLabel(cell, steps[index], index);
        add.text("what", "Loading Backwards", [what] { return what.substr(what.find(' ') + 1); }, nullptr);
        return;
    }
    auto step = [tip, load, index]() -> S& {
        std::vector<S>& steps = load ? tip().loadSteps : tip().unloadSteps;
        return steps[index];
    };
    if (index >= (load ? tip().loadSteps : tip().unloadSteps).size()) return;
    const Strings kinds{ S::kindName(S::Kind::Move), S::kindName(S::Kind::SafeZ), S::kindName(S::Kind::Actuator),
                         S::kindName(S::Kind::Wait), S::kindName(S::Kind::Ask) };
    add.choice("kind", "Kind", kinds, [step] { return std::string(S::kindName(step().kind)); },
               [step](const std::string& v) {
                   for (S::Kind k : { S::Kind::Move, S::Kind::SafeZ, S::Kind::Actuator, S::Kind::Wait, S::Kind::Ask })
                       if (v == S::kindName(k)) step().kind = k;
               });
    f.reshaping.push_back("kind");
    auto speed = [&add, step] {
        add.integer("speed", "Speed [%]", [step] { return int(std::lround(step().speed * 100)); },
                    [step](int v) { step().speed = std::clamp(v, 1, 100) / 100.0; }, 1, 100);
    };
    switch (step().kind) {
        case S::Kind::Move:
            // Where the nozzle doing the change goes, as the axes have it.
            add.group("Location");
            add.header(kXYZR);
            add.row("Location", Place::Location);
            coordinate(add, "x", "X", [step]() -> std::optional<double>& { return step().x; });
            coordinate(add, "y", "Y", [step]() -> std::optional<double>& { return step().y; });
            coordinate(add, "z", "Z", [step]() -> std::optional<double>& { return step().z; });
            coordinate(add, "rotation", "Rotation", [step]() -> std::optional<double>& { return step().rotation; });
            add.end();
            add.note("Where the nozzle goes, in the axes' own coordinates. A coordinate left empty stays as it is.");
            speed();
            break;
        case S::Kind::SafeZ:
            speed();
            break;
        case S::Kind::Actuator:
            add.byName("actuator", "Actuator", named(cell.actuators, "(none)"), [step]() -> std::string& { return step().actuatorId; });
            add.choice("on", "Switch It", { "on", "off" }, [step] { return std::string(step().on ? "on" : "off"); },
                       [step](const std::string& v) { step().on = v == "on"; });
            break;
        case S::Kind::Wait:
            add.integer("waitMs", "Wait [ms]", [step]() -> int& { return step().waitMs; }, 0, 600000);
            break;
        case S::Kind::Ask:
            add.text("message", "Message", [step]() -> std::string& { return step().message; }, "long");
            break;
    }
}

// A calibration's results, and its measurements as graphs: each against the
// fit in the order measured, all of them about the fit (the outlier limit as a
// circle), and how far off each is across the picture.
// `workingZ`: the camera's Default Working Plane Z; `onHead`: a camera on a head.
void calibrationResults(JPFormBuilder& add, const JPCameraCalibration& cal, bool looksUp, double workingZ, bool onHead) {
    const std::string size = std::to_string(cal.width) + "\xC3\x97" + std::to_string(cal.height);
    const std::string key = "cal" + std::to_string(cal.width) + "x" + std::to_string(cal.height) + ".";
    auto shown = [&add, &key](const std::string& name, const std::string& label, const std::string& value) {
        add.text(key + name, label, [value] { return value; }, nullptr);
    };
    char b[160];
    add.group("Results at " + size);
    shown("when", "Measured", cal.when);
    std::snprintf(b, sizeof b, "%.3f", cal.z);
    shown("z", "At Z", b);
    const double umX = 1000 / cal.scaleX(), umY = 1000 / cal.scaleY();
    // In the System Units, as OpenPnP shows them (micrometres are a millimetre's).
    if (JPSystemUnits::inches())
        std::snprintf(b, sizeof b, "%.6f \xC3\x97 %.6f in", JPSystemUnits::shown(umX / 1000), JPSystemUnits::shown(umY / 1000));
    else
        std::snprintf(b, sizeof b, "%.2f \xC3\x97 %.2f \xC2\xB5m", umX, umY);
    shown("upp", "Units Per Pixel", b);
    std::snprintf(b, sizeof b, "%.3f \xC3\x97 %.3f px/mm", cal.scaleX(), cal.scaleY());
    shown("scale", "Scale", b);
    if (JPSystemUnits::inches())
        std::snprintf(b, sizeof b, "%.6f in (%.3f px)", JPSystemUnits::shown(cal.rmsPx * (umX + umY) / 2000), cal.rmsPx);
    else
        std::snprintf(b, sizeof b, "%.2f \xC2\xB5m (%.3f px)", cal.rmsPx * (umX + umY) / 2, cal.rmsPx);
    shown("accuracy", "Estimated Locating Accuracy", b);
    std::snprintf(b, sizeof b, "%.*f \xC3\x97 %.*f %s", JPSystemUnits::places(2), JPSystemUnits::shown(cal.width / cal.scaleX()),
                  JPSystemUnits::places(2), JPSystemUnits::shown(cal.height / cal.scaleY()), JPSystemUnits::suffix());
    shown("fov", "Field of View", b);
    std::snprintf(b, sizeof b, "%.3f deg%s", cal.rotationDeg(looksUp), cal.mirrored(looksUp) ? ", mirrored" : "");
    shown("turn", "Mounting Error (turned)", b);
    std::snprintf(b, sizeof b, "k1 %.4g, k2 %.4g", cal.lensK1, cal.lensK2);
    shown("lens", "Lens", b);
    std::snprintf(b, sizeof b, "%+.1f, %+.1f px", cal.lensCentreX - cal.width / 2.0, cal.lensCentreY - cal.height / 2.0);
    shown("lensCentre", "Lens Centre (from middle)", b);
    std::snprintf(b, sizeof b, "%zu, %d left out, %d missed", cal.points.size(), cal.leftOut, cal.unmeasured);
    shown("points", "Measurements", b);
    if (cal.twoHeights()) {
        std::snprintf(b, sizeof b, "Z %.3f: %.3f px/mm", cal.secondZ, cal.secondScale);
        shown("second", "Second Height", b);
        std::snprintf(b, sizeof b, "Z %.2f", cal.cameraZ());
        shown("cameraZ", "Camera At", b);
        const double f = cal.focalPx();
        std::snprintf(b, sizeof b, "%.1f px", f);
        shown("focal", "Focal Length", b);
        std::snprintf(b, sizeof b, "%.2f \xC3\x97 %.2f deg", 2 * std::atan(cal.width / (2 * f)) * 180 / M_PI,
                      2 * std::atan(cal.height / (2 * f)) * 180 / M_PI);
        shown("fovDeg", "Field of View (angle)", b);
        std::snprintf(b, sizeof b, "%.3f%% a mm nearer", 100 * (cal.scaleAt(cal.z + (cal.cameraZ() > cal.z ? 1 : -1)) / cal.scale() - 1));
        shown("perMm", "Scale Change", b);
        add.note("Camera At: its centre of projection, from how the scale changes between the two heights.");
    }
    // OpenPnP's Camera Mounting Error, and where the camera looks at its working plane.
    if (cal.leans()) {
        add.header({ "X Axis", "Y Axis", "Z Axis" });
        add.row("Camera Mounting Error [Deg]");
        std::snprintf(b, sizeof b, "%.3f", cal.tiltAboutXDeg());
        shown("tiltX", "Mounting Error about X", b);
        std::snprintf(b, sizeof b, "%.3f", cal.tiltAboutYDeg());
        shown("tiltY", "Mounting Error about Y", b);
        std::snprintf(b, sizeof b, "%.3f", cal.rotationDeg(looksUp));
        shown("tiltZ", "Mounting Error about Z", b);
        add.end();
        add.endColumns();
        add.note("How far the camera is tipped from looking straight along Z, about the machine's X and Y axes "
                 "(right hand rule), from where the middle of the picture looked at the two heights; about Z, how "
                 "far it is turned. Tipped, it sees what is lower or higher a little to the side: what it looks at "
                 "is placed by the Default Working Plane Z. A large error does not cost accuracy, only how much of "
                 "an object centred on the reticle stays in view: set the camera straighter and calibrate again.");
    }
    if (cal.looked) {
        const JPCameraCalibration at = cal.atHeight(workingZ);
        add.header({ "X", "Y" });
        add.row(onHead ? "Calibrated Head Offsets" : "Camera Location");
        std::snprintf(b, sizeof b, "%+.*f", JPSystemUnits::places(3), JPSystemUnits::shown(at.lookedX));
        shown("lookedX", "Looked X", b);
        std::snprintf(b, sizeof b, "%+.*f", JPSystemUnits::places(3), JPSystemUnits::shown(at.lookedY));
        shown("lookedY", "Looked Y", b);
        add.end();
        add.endColumns();
        add.note(std::string("Where the middle of the picture looks at the Default Working Plane Z, against where the "
                             "camera's ") + (onHead ? "offsets on the head say" : "place says")
                 + ". The part at the calibration height is part of what visual homing and the nozzle offsets "
                   "were measured by, and stays; only the lean between heights is applied.");
    }
    if (cal.points.empty()) {
        add.note("Calibrate again to see its measurements as graphs.");
        return;
    }
    auto order = std::make_shared<JPPlot>();
    order->kind = JPPlot::Kind::Lines;
    order->xTitle = "measurement";
    order->yTitle = "px";
    JPPlot::Series sx{ "X", JPPlot::Tone::First, {} }, sy{ "Y", JPPlot::Tone::Second, {} };
    auto scatter = std::make_shared<JPPlot>();
    scatter->kind = JPPlot::Kind::Scatter;
    scatter->xTitle = "X px";
    scatter->yTitle = "Y px";
    scatter->circle = cal.outlierPx;
    JPPlot::Series kept{ "measured", JPPlot::Tone::First, {} }, out{ "left out", JPPlot::Tone::Muted, {} };
    auto map = std::make_shared<JPPlot>();
    map->kind = JPPlot::Kind::Map;
    map->yTitle = "px";
    map->width = cal.width;
    map->height = cal.height;
    for (size_t i = 0; i < cal.points.size(); ++i) {
        const JPCameraCalibration::Point& p = cal.points[i];
        sx.points.push_back({ double(i + 1), p.dxPx });
        sy.points.push_back({ double(i + 1), p.dyPx });
        (p.leftOut ? out : kept).points.push_back({ p.dxPx, -p.dyPx });
        if (!p.leftOut) map->spots.push_back({ p.xPx, p.yPx, std::hypot(p.dxPx, p.dyPx) });
    }
    order->series = { sx, sy };
    scatter->series = { kept };
    if (!out.points.empty()) scatter->series.push_back(out);
    add.plot("Residual Errors in the Order Measured", order);
    add.note("How far each measurement is from where the fit puts it, along X and Y. They should look like noise about "
             "zero: a step or a drift says something moved while measuring (the mark, the camera in its mount, a "
             "missed step, a slipping belt, warming up).");
    add.plot("Residual Errors, X against Y", scatter);
    add.note("All of them together: one round cluster about the middle. The circle is the outlier limit; outside "
             "it, a measurement was left out. Two clusters or a stretched one: the mark was found badly, or the "
             "backlash or a drive is out.");
    add.plot("Residual Error Map", map);
    add.note("How far off the measurements are across the picture, coolest to hottest (the colours only show "
             "where, not how much). It should look patchy at random; rings or stripes mean the lens fits the "
             "picture badly there.");
}

void cameraForm(JPCellConfig& cell, const std::string& id, JPSetupProperties::Form& f, const JPSetupProperties::Live& live) {
    auto c = finder(cell.cameras, id);
    f.title = "Camera " + c().name;
    JPFormBuilder add(f);
    add.tab("General Configuration");
    add.group("Properties");
    add.text("name", "Name", [c]() -> std::string& { return c().name; }, "name");
    add.choice("looking", "Looking", { "Down", "Up" }, [c] { return std::string(c().looksUp ? "Up" : "Down"); },
               [c](const std::string& v) { c().looksUp = v == "Up"; });
    add.number("previewFps", "Preview FPS", [c]() -> double& { return c().previewFps; }, 1);
    add.tip("How many times a second the live picture is shown, at most (0: every picture the camera gives).");
    add.flag("suspendDuringTasks", "Suspend during tasks?", [c]() -> bool& { return c().suspendDuringTasks; });
    add.tip("Continuous camera preview is suspended during machine tasks, only frames captured using computer vision "
            "are shown. For high Preview FPS this improves performance");
    add.flag("autoCameraView", "Auto Camera View?", [c]() -> bool& { return c().autoCameraView; });
    add.tip("If enabled, the CameraView will be automatically selected whenever a user action is related to the camera "
            "or when a computer vision result is presented.");
    add.flag("shownInMultiView", "Show in multi camera view?", [c]() -> bool& { return c().shownInMultiView; });
    add.tip("Show this camera in the Camera Panel when mutiple cameras are shown. For example this can be switched off for "
            "capture card cameras that are already exposed through SwitcherCameras. (Off, its window starts closed.)");
    if (c().mount.headId.empty()) {
        add.choice("focusSensingMethod", "Focus Sensing Method", { "None", "AutoFocus" }, [c] { return c().focusSensingMethod; },
                   [c](const std::string& v) { c().focusSensingMethod = v; });
        f.reshaping.push_back("focusSensingMethod");
    }
    auto device = [c]() -> JJson& { return c().device; };
    add.group("Light");
    {
        // A camera on a head: the head's actuators, and the machine's only when allowed (OpenPnP's, for older
        // scripts' lights) or one of them is the light already.
        static bool allowMachine = false;
        const std::string headId = c().mount.headId, current = std::as_const(device())["light-actuator-id"].str();
        bool machineLight = false;
        for (const JPActuatorConfig& a : cell.actuators)
            if (a.id == current && a.mount.headId.empty()) machineLight = true;
        const bool listMachine = headId.empty() || allowMachine || machineLight;
        JPFormBuilder::Named lights;
        lights.add("(none)", "");
        for (const JPActuatorConfig& a : cell.actuators)
            if (a.mount.headId == headId || (listMachine && a.mount.headId.empty())) lights.add(a.name.empty() ? a.id : a.name, a.id);
        add.row("Light Actuator");
        add.byName("light", "Light Actuator", lights, [device] { return std::as_const(device())["light-actuator-id"].str(); },
                   [device](const std::string& v) { device()["light-actuator-id"] = v; });
        f.reshaping.push_back("light");
        if (!headId.empty()) {
            add.flag("lightAllowMachine", "Allow Machine Actuators?", [machineLight] { return allowMachine || machineLight; },
                     [](bool on) { allowMachine = on; });
            add.tip("It is recommended to attach the Light Actuator to the camera's head. However, for backwards-compatibility "
                    "with how Light Actuators were used in Scripts, you can enable this switch and choose a Machine actuator.");
            f.reshaping.push_back("lightAllowMachine");
            f.viewOnly.push_back("lightAllowMachine");
        }
        add.end();
    }
    auto light = [c]() -> JPCameraConfig::Light& { return c().light; };
    add.header({ "ON", "OFF" });
    add.row("Before Capture?");
    add.flag("lightBeforeCapture", "Before Capture?", [light]() -> bool& { return light().beforeCapture; });
    add.flag("lightAfterCapture", "After Capture?", [light]() -> bool& { return light().afterCapture; });
    add.end();
    add.row("User Camera Action?");
    add.flag("lightUserAction", "User Camera Action?", [light]() -> bool& { return light().userAction; });
    add.flag("lightAntiGlare", "Anti-Glare?", [light]() -> bool& { return light().antiGlare; });
    add.end();
    add.note("ON: before a picture is taken for vision; while you are looking at the camera. OFF: after the "
             "picture for vision; while another camera takes one (anti-glare).");
    add.group("Units Per Pixel");
    // A start for calibrating with a nozzle's tip, whose size is not known.
    add.header({ "X", "Y" });
    add.row("Units per Pixel");
    add.length("unitsPerPixelX", "Units per Pixel X", [c]() -> double& { return c().unitsPerPixelX; }, 5);
    add.length("unitsPerPixelY", "Units per Pixel Y", [c]() -> double& { return c().unitsPerPixelY; }, 5);
    add.end();
    add.note("A rough start: calibrating measures them.");
    add.group("When the Camera Is Lost");
    auto lost = [c]() -> JPCameraConfig::Lost& { return c().lost; };
    add.integer("lostNoPicture", "No Picture For (s)", [lost]() -> int& { return lost().noPictureS; }, 1, 60);
    add.integer("lostSamePicture", "Same Picture For (s)", [lost]() -> int& { return lost().samePictureS; }, 0, 60);
    add.integer("lostWait", "Work Waits For It (s)", [lost]() -> int& { return lost().waitS; }, 0, 600);
    add.note("A camera that sends no picture, or the very same picture over and over, has hung or dropped off: "
             "it is opened again until it is back. 0 for the same picture: never counted, for a camera that can "
             "show a still scene exactly alike. Work looking through it waits for it, then stops saying the "
             "camera was lost (0: at once).");

    add.tab("Camera Settling");
    add.group("Camera Settling");
    auto settle = [c]() -> JPCameraConfig::Settle& { return c().settle; };
    add.row("Settle Method");
    add.choice("settleMethod", "Settle Method", { "FixedTime", "Maximum", "Mean", "Euclidean", "Square", "Motion" },
               [settle] { return settle().method; }, [settle](const std::string& v) { settle().method = v; });
    if (settle().method == "FixedTime")
        add.integer("settleTimeMs", "Settle Time (ms)", [settle]() -> int& { return settle().timeMs; }, 0, 10000);
    else
        add.integer("settleTimeoutMs", "Settle Timeout (ms)", [settle]() -> int& { return settle().timeoutMs; }, 0, 60000);
    add.end();
    f.reshaping.push_back("settleMethod");
    if (settle().method == "FixedTime") {
        add.note("A picture for vision is one taken this long after the move ended.");
    } else {
        add.row("Settle Threshold");
        add.number("settleThreshold", "Settle Threshold", [settle]() -> double& { return settle().threshold; }, 3);
        add.integer("settleDebounce", "Debounce Frames", [settle]() -> int& { return settle().debounce; }, 0, 100);
        add.end();
        add.row("Color Sensitive?");
        add.flag("settleFullColor", "Color Sensitive?", [settle]() -> bool& { return settle().fullColor; });
        add.tip("Compare as full color image, i.e. difference in colors with same brightness will register.");
        add.flag("settleGradients", "Edge Sensitive?", [settle]() -> bool& { return settle().gradients; });
        add.tip("Use the gradients of the images rather than brightness.");
        add.end();
        add.row("Enhance Contrast");
        add.number("settleContrastEnhance", "Enhance Contrast", [settle]() -> double& { return settle().contrastEnhance; }, 2);
        add.tip("How much it should enhance the contrast from 0.0 (original image) to 1.0 (full dynamic range).");
        add.integer("settleGaussianBlur", "Denoise (Pixel)", [settle]() -> int& { return settle().gaussianBlur; }, 0, 999);
        add.tip("Diameter in pixels of the Gaussian Blur used to denoise the images. For large diameters the image will be "
                "scaled down for better speed.");
        add.end();
        add.row("Center Mask");
        add.number("settleMaskCircle", "Center Mask", [settle]() -> double& { return settle().maskCircle; }, 3);
        add.tip("Size of the central circular mask, relative to the camera dimension (height or width, whichever is smaller). "
                "0.0 no mask; 0.5 circular center area of half the camera view; 1.0 circular center area to the edge of the "
                "camera view; 1.5 circular area vignetting the camera view.");
        add.flag("settleDiagnostics", "Diagnostics?", [settle]() -> bool& { return settle().diagnostics; });
        add.tip("Enable graphical diagnostics and replay of settle frames.");
        add.end();
        add.note(settle().method == "Motion"
                     ? "Each picture is looked for in the one before: how many pixels it moved (more than a twentieth of "
                       "it, no match, the most), until that stays under the threshold for Debounce Frames more pictures, "
                       "or the timeout passes."
                     : "Each picture is compared with the one before (as a percentage of full scale), until the "
                       "difference stays under the threshold for Debounce Frames more pictures, or the timeout passes.");
    }
    add.group("Test");
    const bool fixedCamera = c().mount.headId.empty();
    std::vector<std::pair<std::string, std::string>> tests { { "Left", "settleTestLeft" }, { "Right", "settleTestRight" },
                                                             { "Back", "settleTestBack" }, { "Front", "settleTestFront" },
                                                             { "Here", "settleTestHere" } };
    // A fixed camera's: the nozzle turned, and the nozzle brought over it (OpenPnP's Up).
    if (fixedCamera) {
        tests.push_back({ "Rotate", "settleTestRotate" });
        tests.push_back({ "Up", "settleTestUp" });
    }
    add.actions(tests);
    add.note(fixedCamera
                 ? "Move the nozzle chosen on the Jog pad one jog step (the Jog pad's distance) that way and back, turn "
                   "it one jog step and back (Rotate), or not at all (Here), and let the camera settle as a picture for "
                   "vision would, and graph how it came to rest. Up brings the nozzle over the camera at Safe Z (already "
                   "there, it goes up to Safe Z and back). Put the nozzle over the camera, at its focus, first: only X "
                   "and Y move, and no move goes to Safe Z."
                 : "Move the camera one jog step (the Jog pad's distance) that way and back, or not at all (Here), "
                   "let it settle as a picture for vision would, and graph how it came to rest.");
    if (const auto& t = c().settleTrace) {
        auto g = std::make_shared<JPPlot>();
        g->kind = JPPlot::Kind::Lines;
        g->xTitle = "ms";
        g->yTitle = t->method == "Motion" ? "motion px" : "difference %";
        JPPlot::Series d{ "difference", JPPlot::Tone::First, {} };
        for (const auto& [ms, v] : t->points) d.points.push_back({ ms, v });
        g->series.push_back(d);
        if (t->threshold > 0 && !t->points.empty())
            g->series.push_back({ "threshold", JPPlot::Tone::Second,
                                  { { t->points.front().first, t->threshold }, { t->points.back().first, t->threshold } } });
        double top = t->threshold;
        for (const auto& [ms, v] : t->points) top = std::max(top, v);
        if (t->settledMs >= 0) g->series.push_back({ "settled", JPPlot::Tone::Muted, { { t->settledMs, 0 }, { t->settledMs, top } } });
        // OpenPnP's Capture row: when each picture was taken, a tick low down.
        if (!t->captures.empty()) {
            JPPlot::Series capture { "capture", JPPlot::Tone::Third, {}, true };
            for (const auto& [ms, on] : t->captures) capture.points.push_back({ ms, on });
            g->series.push_back(capture);
            g->y2Lo = -kCaptureBandBelow;
            g->y2Hi = 1 + kCaptureBandAbove;
        }
        char title[120];
        if (t->settledMs >= 0) std::snprintf(title, sizeof title, "Settled after %.0f ms (%s)", t->settledMs, t->method.c_str());
        else std::snprintf(title, sizeof title, "Not settled within the timeout (%s)", t->method.c_str());
        add.plot(title, g);
        add.note(t->method == "FixedTime"
                     ? "With FixedTime, the pictures' Euclidean difference over the fixed wait: where it falls flat "
                       "is how long the camera takes to come to rest."
                     : "Each picture's difference from the one before; the camera is still once it stays under the "
                       "threshold. A threshold just above the flat part, with a little room, settles soonest.");
        // OpenPnP's replay: the pictures as they were compared, one at a time.
        if (t->pictures && !t->pictures->empty()) {
            const int count = int(t->pictures->size());
            auto trace = [c]() -> JPSettleTrace& { return *c().settleTrace; };
            add.slider("settleReplay", "Replay", 1, count, [trace, count] { return std::clamp(trace().replay, 0, count - 1) + 1; },
                       [trace](int v) { trace().replay = v - 1; });
            f.reshaping.push_back("settleReplay");
            f.viewOnly.push_back("settleReplay");
            const JPSettleTrace::Picture& p = (*t->pictures)[size_t(std::clamp(t->replay, 0, count - 1))];
            auto picture = std::make_shared<JPFrame>();
            picture->width = p.width;
            picture->height = p.height;
            picture->rgba.reserve(size_t(p.width) * size_t(p.height) * 4);
            for (size_t i = 0; i + size_t(p.channels) <= p.pixels.size(); i += size_t(p.channels)) {
                // Grey, or OpenCV's blue, green, red.
                const uint8_t r = p.channels == 1 ? p.pixels[i] : p.pixels[i + 2], gr = p.channels == 1 ? p.pixels[i] : p.pixels[i + 1];
                const uint8_t b = p.pixels[i];
                picture->rgba.insert(picture->rgba.end(), { r, gr, b, 255 });
            }
            std::shared_ptr<const JPFrame> shown = picture;
            add.image("", [shown] { return shown; });
            double diff = -1;
            for (const auto& [ms, v] : t->points)
                if (ms <= p.ms + kSameMs) diff = v;
            char at[120];
            if (diff >= 0) std::snprintf(at, sizeof at, "Picture taken at %.0f ms, %.3f from the one before.", p.ms, diff);
            else std::snprintf(at, sizeof at, "Picture taken at %.0f ms, the first.", p.ms);
            add.note(at);
        }
    }

    add.tab("Device Settings");
    // A picture's settings are OpenPnP's groups of their own.
    if (std::as_const(device())["backend"].str() != "image" && !JPSimulatedUpCamera::is(std::as_const(device()))) add.group("Device");
    if (std::as_const(device())["backend"].str() == "image") {
        // OpenPnP's ImageCameraConfigurationWizard: a picture of the table, shown where the camera looks.
        auto num = [device](const char* a, const char* b, double def) {
            return std::pair { [device, a, b, def] {
                                   const JJson& d = std::as_const(device());
                                   return b ? d[a][b].number(def) : d[a].number(def);
                               },
                               [device, a, b](double v) {
                                   if (b) device()[a][b] = v;
                                   else device()[a] = v;
                               } };
        };
        constexpr double kDefaultFocalLengthMm = 6, kDefaultSensorDiagonalMm = 4.4;   // OpenPnP's
        add.group("Camera Simulation");
        add.header({ "X", "Y" });
        add.row("Pixel Dimension");
        add.integer("width", "Width", [device] { return int(std::as_const(device())["width"].number(640)); },
                    [device](int v) { device()["width"] = v; }, 1, 10000);
        add.integer("height", "Height", [device] { return int(std::as_const(device())["height"].number(480)); },
                    [device](int v) { device()["height"] = v; }, 1, 10000);
        add.end();
        add.row("Simulated Units per Pixel");
        add.length("imageUppX", "Units Per Pixel X", num("imageUnitsPerPixel", "x", 0.04).first, num("imageUnitsPerPixel", "x", 0.04).second, 6);
        add.length("imageUppY", "Units Per Pixel Y", num("imageUnitsPerPixel", "y", 0.04).first, num("imageUnitsPerPixel", "y", 0.04).second, 6);
        add.end();
        add.tip("To allow simulation of Unit per Pixel calibration, the true Units per Pixel of the image must be stored independently.");
        add.row("Offset");
        add.length("imageOffsetX", "Offset X", num("imageOffset", "x", 0).first, num("imageOffset", "x", 0).second);
        add.length("imageOffsetY", "Offset Y", num("imageOffset", "y", 0).first, num("imageOffset", "y", 0).second);
        add.end();
        add.tip("Offset applied between calculated and used pixel in picture. Used to shift the image if required.");
        add.endColumns();
        add.number("simulatedRotation", "Z Rotation", num("simulatedRotation", nullptr, 0).first, num("simulatedRotation", nullptr, 0).second);
        add.tip("Simulated camera mounting rotation around the Z axis (Portrait/Landscape/mounting error).");
        add.number("simulatedYRotation", "Y Rotation", num("simulatedYRotation", nullptr, 0).first, num("simulatedYRotation", nullptr, 0).second);
        add.tip("Simulated camera mounting error as a rotation around the Y axis (sideways tilt).");
        add.number("simulatedScale", "Viewing Scale", num("simulatedScale", nullptr, 1).first, num("simulatedScale", nullptr, 1).second);
        add.number("simulatedDistortion", "Distortion [%]", num("simulatedDistortion", nullptr, 0).first, num("simulatedDistortion", nullptr, 0).second);
        add.tip("Simulated lens distortion. Positive values create Barrel distortion, negative values create a Pincushion distortion.");
        add.flag("simulatedFlipped", "View mirrored?", [device] { return std::as_const(device())["simulatedFlipped"].boolean(); },
                 [device](bool v) { device()["simulatedFlipped"] = v; });
        add.tip("Simulate the camera as showing a mirrored view");
        add.row("Source URL");
        add.text("source", "Source URL", [device] { return std::as_const(device())["source"].str(); },
                 [device](const std::string& v) { device()["source"] = v; }, "long");
        add.button("browseImageSource", "Browse", "Choose the picture of the machine's table (PNG).");
        add.end();
        add.group("Simulated Calibration Rig");
        add.row("Focal Length");
        add.length("focalLength", "Focal Length", num("focalLengthMm", nullptr, kDefaultFocalLengthMm).first,
                   num("focalLengthMm", nullptr, kDefaultFocalLengthMm).second);
        add.length("sensorDiagonal", "Sensor Diagonal", num("sensorDiagonalMm", nullptr, kDefaultSensorDiagonalMm).first,
                   num("sensorDiagonalMm", nullptr, kDefaultSensorDiagonalMm).second);
        add.end();
        add.tip("Lens focal length, and the imaging sensor's diagonal for relation with it (e.g. 1/4\" 4.5 mm, 1/3\" 6.0 mm, "
                "1/2.5\" 7.2 mm, 1/2\" 8.0 mm).");
        add.header({ "X", "Y", "Z" });
        for (const auto& [key, label] : { std::pair { "primaryFiducial", "Primary Fiducial" }, std::pair { "secondaryFiducial", "Secondary Fiducial" } }) {
            add.row(label);
            for (const char* axis : { "x", "y", "z" })
                add.length(std::string(key) + "." + axis, std::string(label) + " " + axis, num(key, axis, 0).first,
                           [device, key, axis](double v) {
                               device()[key][axis] = v;
                               // All at the origin: none, as OpenPnP's.
                               const JJson& f = std::as_const(device())[key];
                               if (f["x"].number(0) == 0 && f["y"].number(0) == 0 && f["z"].number(0) == 0) {
                                   JJson rest = JJson::object();
                                   for (const auto& [n, val] : std::as_const(device()).obj()) if (n != key) rest[n] = val;
                                   device() = rest;
                               }
                           });
            add.end();
        }
        add.endColumns();
        add.note("Two 1 mm white fiducials drawn into the picture where they are on the machine (none at the origin), the "
                 "secondary at its own height: as far from the camera as the lens and sensor make it, smaller or bigger, "
                 "blurred, and moved by the Y Rotation. For trying the camera's calibration.");
    } else if (std::as_const(device())["backend"].str() == "mjpg") {
        // OpenPnP's MjpgCaptureCameraWizard.
        add.text("url", "MJPG URL", [device] { return std::as_const(device())["url"].str(); },
                 [device](const std::string& v) { device()["url"] = v; }, "long");
        add.tip("The camera's stream: http://host:port/path.");
        add.integer("timeoutMs", "Timeout [ms]", [device] { return int(std::as_const(device())["timeoutMs"].number(3000)); },
                    [device](int v) { device()["timeoutMs"] = v; }, 100, 60000);
    } else if (std::as_const(device())["backend"].str() == "gstreamer") {
        // OpenPnP's GstreamerCameraConfigurationWizard.
        add.text("gstPipeline", "Pipeline launch string", [device] { return std::as_const(device())["pipeline"].str(); },
                 [device](const std::string& v) { device()["pipeline"] = v; }, "long");
        add.note("As gst-launch-1.0 is given one (\"v4l2src device=/dev/video0 ! image/jpeg,width=1280,height=720 ! "
                 "jpegdec\"): its pictures, turned to colour, are this camera's, at the size and rate the pipeline "
                 "gives. GStreamer must be installed on the computer.");
    } else if (std::as_const(device())["backend"].str() == "onvif") {
        // OpenPnP's OnvifIPCameraConfigurationWizard.
        auto field = [device](const char* key) {
            return std::pair { [device, key] { return std::as_const(device())[key].str(); },
                               [device, key](const std::string& v) { device()[key] = v; } };
        };
        add.text("onvifHost", "Camera IP", field("host").first, field("host").second);
        add.tip("(IP:port)");
        add.text("onvifUsername", "Username", field("username").first, field("username").second);
        add.tip("(normally required)");
        add.text("onvifPassword", "Password", field("password").first, field("password").second);
        add.tip("(leave blank for none)");
        JPFormBuilder::Strings resolutions { "" };
        for (const JPOnvif::Resolution& r : JPOnvif::knownResolutions(std::as_const(device())["host"].str()))
            resolutions.push_back(r.text());
        add.editableChoice("onvifResolution", "Resolution", resolutions, field("preferredResolution").first,
                           field("preferredResolution").second);
        add.tip("(only supported resolutions shown)");
        add.integer("onvifResizeWidth", "Target Width", [device] { return int(std::as_const(device())["resizeWidth"].number(0)); },
                    [device](int v) { device()["resizeWidth"] = v; }, 0, 100000);
        add.tip("(Use 0 for no resizing)");
        add.integer("onvifResizeHeight", "Target Height", [device] { return int(std::as_const(device())["resizeHeight"].number(0)); },
                    [device](int v) { device()["resizeHeight"] = v; }, 0, 100000);
        add.tip("(Use 0 for no resizing)");
        add.note("The camera is set to the Resolution (empty: its largest) in its first JPEG profile, at its best "
                 "quality and fastest rate, and its snapshots are fetched one after another. The resolutions it "
                 "offers are listed once it has been opened.");
    } else if (std::as_const(device())["backend"].str() == "switcher") {
        // OpenPnP's SwitcherCameraConfigurationWizard.
        JPFormBuilder::Named sources;
        for (const JPCameraConfig& other : cell.cameras)
            if (other.id != id) sources.add(other.name.empty() ? other.id : other.name, other.id);
        add.byName("switchedCamera", "Source Camera", sources, [device] { return std::as_const(device())["camera"].str(); },
                   [device](const std::string& v) { device()["camera"] = v; });
        add.tip("The capture device the multiplexer feeds: its pictures, as taken, are this camera's.");
        add.integer("switcher", "Switcher Number", [device] { return int(std::as_const(device())["switcher"].number(0)); },
                    [device](int v) { device()["switcher"] = v; }, 0, 1000);
        add.tip("Cameras on the same multiplexer share a number.");
        add.byName("switcherActuator", "Actuator", named(cell.actuators, "(none)"),
                   [device] { return std::as_const(device())["actuator"].str(); },
                   [device](const std::string& v) { device()["actuator"] = v; });
        add.number("actuatorValue", "Actuator Value", [device] { return std::as_const(device())["actuatorValue"].number(0); },
                   [device](double v) { device()["actuatorValue"] = v; });
        add.tip("What the actuator is set to, to switch this camera in (on/off for a boolean actuator: not 0 is on).");
        add.integer("actuatorDelayMs", "Actuator Delay (ms)", [device] { return int(std::as_const(device())["actuatorDelayMs"].number(500)); },
                    [device](int v) { device()["actuatorDelayMs"] = v; }, 0, 60000);
        add.tip("How long the picture takes to come through after switching.");
    } else if (std::as_const(device())["backend"].str() == "neoden4") {
        // OpenPnP's Neoden4CameraConfigurationWizard: General and Image.
        auto whole = [device](const char* key, int def) {
            return std::pair { [device, key, def] { return int(std::as_const(device())[key].number(def)); },
                               [device, key](int v) { device()[key] = v; } };
        };
        constexpr int kMostPixels = 10000, kMostTimeoutMs = 600000;
        add.integer("neodenCameraId", "Camera Id", whole("cameraId", 1).first, whole("cameraId", 1).second, 0, 255);
        add.tip("Which of the NeoDen's cameras: 1 looks down, 5 looks up.");
        add.integer("neodenTimeoutMs", "Timeout", whole("timeoutMs", 1000).first, whole("timeoutMs", 1000).second, 1, kMostTimeoutMs);
        add.tip("(millisecs)");
        add.group("Image");
        add.integer("neodenWidth", "Width", whole("width", 1024).first, whole("width", 1024).second, 1, kMostPixels);
        add.integer("neodenShiftX", "Shift X", whole("shiftX", 0).first, whole("shiftX", 0).second, 0, kMostPixels);
        add.tip("(pixels)");
        add.integer("neodenHeight", "Height", whole("height", 1024).first, whole("height", 1024).second, 1, kMostPixels);
        add.integer("neodenShiftY", "Shift Y", whole("shiftY", 0).first, whole("shiftY", 0).second, 0, kMostPixels);
        add.tip("(pixels)");
        add.note("Taken through the NeoDen's camera library (libneodencam.so), which must be installed: grey pictures "
                 "Width x Height, from Shift X, Shift Y on the sensor.");
    } else if (std::as_const(device())["backend"].str() == "neoden4Switcher") {
        // OpenPnP's Neoden4SwitcherCameraConfigurationWizard.
        JPFormBuilder::Named sources;
        for (const JPCameraConfig& other : cell.cameras)
            if (other.id != id && std::as_const(other.device)["backend"].str() == "neoden4")
                sources.add(other.name.empty() ? other.id : other.name, other.id);
        add.byName("neodenSource", "Source Camera", sources, [device] { return std::as_const(device())["camera"].str(); },
                   [device](const std::string& v) { device()["camera"] = v; });
        add.tip("The NeoDen 4 camera whose picture size and timeout it takes.");
        auto whole = [device](const char* key, int def) {
            return std::pair { [device, key, def] { return int(std::as_const(device())[key].number(def)); },
                               [device, key](int v) { device()[key] = v; } };
        };
        add.integer("neodenSwitcher", "Switcher Number", whole("switcher", 0).first, whole("switcher", 0).second, 0, 255);
        add.tip("The NeoDen camera it reads (1 looks down, 5 looks up).");
        add.integer("neodenExposure", "Exposure", whole("exposure", 25).first, whole("exposure", 25).second, 0, 32767);
        add.integer("neodenGain", "Gain", whole("gain", 8).first, whole("gain", 8).second, 0, 32767);
        add.note("Before each picture the device is switched to it, and set to its exposure and gain (a change resets "
                 "the camera).");
    } else if (std::as_const(device())["backend"].str() == "simulated" && JPSimulatedUpCamera::is(std::as_const(device()))) {
        // OpenPnP's SimulatedUpCameraConfigurationWizard.
        using Up = JPSimulatedUpCamera;
        auto value = [device](const char* a, const char* b, double def) {
            return std::pair { [device, a, b, def] {
                                   const JJson& d = std::as_const(device());
                                   return b ? d[a][b].number(def) : d[a].number(def);
                               },
                               [device, a, b](double v) {
                                   if (b) device()[a][b] = v;
                                   else device()[a] = v;
                               } };
        };
        add.group("Camera Simulation");
        add.header({ "X", "Y", "Z", "Rotation" });
        // Its location: where it is set up to be until one of its own is given (as OpenPnP takes it then).
        add.row("Camera Location");
        for (const char* axis : { "x", "y", "z", "rotation" }) {
            auto own = [c, axis] {
                const JPMountConfig& m = c().mount;
                return std::string(axis) == "x" ? m.offsetX : std::string(axis) == "y" ? m.offsetY : std::string(axis) == "z" ? m.offsetZ : 0.0;
            };
            auto get = [device, own, axis] {
                const JJson& l = std::as_const(device())["simulatedLocation"];
                return l.isObject() ? l[axis].number(0) : own();
            };
            auto set = [device, c, axis](double v) {
                // The first change: its own from where it is set up to be, that one axis changed.
                if (!std::as_const(device())["simulatedLocation"].isObject()) {
                    const JPMountConfig& m = c().mount;
                    device()["simulatedLocation"] = JPMachineLocation { m.offsetX, m.offsetY, m.offsetZ, 0 }.toJson();
                }
                device()["simulatedLocation"][axis] = v;
            };
            if (std::string(axis) == "rotation") add.number(std::string("simulatedLocation.") + axis, "Camera Location Rotation", get, set);
            else add.length(std::string("simulatedLocation.") + axis, std::string("Camera Location ") + axis, get, set);
        }
        add.end();
        add.tip("The Camera simulated location. Note: In order to test calibration procedures, we cannot use the regular camera location.");
        add.row("Pixel Dimension");
        add.integer("width", "Width", [device] { return int(std::as_const(device())["width"].number(Up::kDefaultWidth)); },
                    [device](int v) { device()["width"] = v; }, 1, 10000);
        add.integer("height", "Height", [device] { return int(std::as_const(device())["height"].number(Up::kDefaultHeight)); },
                    [device](int v) { device()["height"] = v; }, 1, 10000);
        add.end();
        add.row("Simulated Units per Pixel");
        // As the camera takes them (a cell from before they were kept: from its scene); either set keeps both.
        for (const bool x : { true, false })
            add.length(x ? "simulatedUppX" : "simulatedUppY", x ? "Simulated Units per Pixel X" : "Simulated Units per Pixel Y",
                       [device, x] {
                           const Up::Settings st = Up::Settings::fromDevice(std::as_const(device()));
                           return x ? st.uppX : st.uppY;
                       },
                       [device, x](double v) {
                           const Up::Settings st = Up::Settings::fromDevice(std::as_const(device()));
                           device()["simulatedUnitsPerPixel"]["x"] = x ? v : st.uppX;
                           device()["simulatedUnitsPerPixel"]["y"] = x ? st.uppY : v;
                           device()["simulatedFlipped"] = st.mirrored;
                       }, 6);
        add.end();
        add.tip("The camera simulated units per pixel. Note: In order to test calibration procedures, we cannot use the regular units per pixel.");
        add.length("focalLength", "Focal Length", value("focalLengthMm", nullptr, Up::kDefaultFocalLengthMm).first,
                   value("focalLengthMm", nullptr, Up::kDefaultFocalLengthMm).second);
        add.length("sensorDiagonal", "Sensor Diagonal", value("sensorDiagonalMm", nullptr, Up::kDefaultSensorDiagonalMm).first,
                   value("sensorDiagonalMm", nullptr, Up::kDefaultSensorDiagonalMm).second);
        JPFormBuilder::Strings scenarios;
        for (const Up::Scenario& sc : Up::scenarios()) scenarios.push_back(sc.name);
        add.choice("backgroundScenario", "Background Scenario", scenarios,
                   [device] { const std::string v = std::as_const(device())["backgroundScenario"].str(); return v.empty() ? std::string(Up::kDefaultScenario) : v; },
                   [device](const std::string& v) { device()["backgroundScenario"] = v; });
        add.tip("Choose a background scenario. It simulates a background shade and nozzle tip color.");
        add.row("Pick Error Offsets");
        for (const char* axis : { "x", "y", "z", "rotation" }) {
            if (std::string(axis) == "rotation") add.number("errorOffsets.rotation", "Pick Error Rotation", value("errorOffsets", axis, 0).first, value("errorOffsets", axis, 0).second);
            else add.length(std::string("errorOffsets.") + axis, std::string("Pick Error ") + axis, value("errorOffsets", axis, 0).first, value("errorOffsets", axis, 0).second);
        }
        add.end();
        add.tip("Picked part on nozzle error offsets in simulation.");
        add.endColumns();
        add.flag("simulatedFlipped", "View mirrored?", [device] { return Up::Settings::fromDevice(std::as_const(device())).mirrored; },
                 [device](bool v) {
                     const Up::Settings st = Up::Settings::fromDevice(std::as_const(device()));
                     device()["simulatedUnitsPerPixel"]["x"] = st.uppX;
                     device()["simulatedUnitsPerPixel"]["y"] = st.uppY;
                     device()["simulatedFlipped"] = v;
                 });
        add.tip("Simulate the camera as showing a mirrored view");
        add.flag("simulateFocalBlur", "Simulate Focal Blur?", [device] { return std::as_const(device())["simulateFocalBlur"].boolean(); },
                 [device](bool v) { device()["simulateFocalBlur"] = v; });
        add.tip("Simulate focal blur in order to test Auto Focus.");
    } else if (std::as_const(device())["backend"].str() == "simulated") {
        add.text("backend", "Device", [] { return std::string("simulated (set up in the cell file)"); }, nullptr);
    } else {
        // Found by the name the device gives itself, whichever socket it is in.
        add.text("device", "Device", [device] { return std::as_const(device())["name"].str(); },
                 [device](const std::string& v) { device()["name"] = v; }, "long");
    }
    // A switcher camera takes its device camera's pictures as they come, an ONVIF one is set up by what it
    // offers, a GStreamer one is as its pipeline says, a NeoDen 4 or picture one has its own: none has a size or controls set here.
    if (const std::string& b = std::as_const(device())["backend"].str();
        b != "switcher" && b != "onvif" && b != "gstreamer" && b != "neoden4" && b != "neoden4Switcher" && b != "image"
        && !JPSimulatedUpCamera::is(std::as_const(device()))) {
        // A capture device's own format and settings (OpenPnP's OpenPnpCaptureCamera); a picture or simulation has none.
        const bool captureDevice = b == "v4l2" || b.empty();
        if (captureDevice)
            add.choice("format", "Format", { "any", "MJPG", "YUYV" },
                       [device] { const std::string v = std::as_const(device())["format"].str(); return v.empty() ? std::string("any") : v; },
                       [device](const std::string& v) { device()["format"] = v == "any" ? std::string() : v; });
        add.header({ "Width", "Height" });
        add.row("Size");
        add.integer("width", "Width", [device] { return int(std::as_const(device())["width"].number()); },
                    [device](int v) { device()["width"] = v; }, 0, 10000);
        add.integer("height", "Height", [device] { return int(std::as_const(device())["height"].number()); },
                    [device](int v) { device()["height"] = v; }, 0, 10000);
        add.end();
        add.note("0: the largest picture the camera offers.");
        // OpenPnP's Capture FPS: how fast the camera gives pictures, measured.
        add.row("Capture FPS");
        char fps[32] = "";
        if (c().captureFps) std::snprintf(fps, sizeof fps, "%.1f", *c().captureFps);
        add.words(fps);
        add.button("captureFpsTest", "Test", "Count the pictures the camera gives over two seconds: the average FPS obtained.");
        add.end();

        if (captureDevice) {
            // The camera's own settings: each one jplacer sets when it opens the
            // camera (by hand, or automatic where the camera can), or leaves as the
            // camera has it.
            add.group("Properties");
            // The device's own ranges and defaults, and its values where not set, as the camera reports them.
            const JJson have = live.cameraControls ? live.cameraControls(c().id) : JJson::object();
            add.header({ "Set?", "Auto", "Value", "Min", "Max", "Default" });
            struct Control { const char* key; const char* label; bool canAuto; };
            static const Control kControls[] = {
                { "brightness", "Brightness", true }, { "backlight-compensation", "Backlight Compensation", false },
                { "contrast", "Contrast", false }, { "exposure", "Exposure", true }, { "focus", "Focus", true },
                { "gain", "Gain", true }, { "gamma", "Gamma", false }, { "hue", "Hue", true },
                { "power-line-frequency", "Power Line Freq.", false }, { "saturation", "Saturation", false },
                { "sharpness", "Sharpness", false }, { "white-balance", "White Balance", true }, { "zoom", "Zoom", false } };
            for (const Control& k : kControls) {
                auto control = [device, key = std::string(k.key)]() -> JJson& { return device()["controls"][key]; };
                const bool set = std::as_const(device())["controls"][k.key].isObject();
                const std::string name = std::string("control:") + k.key;
                add.row(k.label);
                add.flag(name, std::string(k.label) + " set", [device, key = std::string(k.key)] { return std::as_const(device())["controls"][key].isObject(); },
                         [device, control, key = std::string(k.key)](bool on) {
                             if (!on) {
                                 JJson rest = JJson::object();
                                 for (const auto& [n, v] : std::as_const(device())["controls"].obj()) if (n != key) rest[n] = v;
                                 device()["controls"] = rest;
                             } else if (!std::as_const(device())["controls"][key].isObject()) {
                                 control()["auto"] = false;
                                 control()["value"] = 0;
                             }
                         });
                f.reshaping.push_back(name);
                if (set && k.canAuto)
                    add.flag(name + ":auto", std::string(k.label) + " auto", [control] { return std::as_const(control())["auto"].boolean(); },
                             [control](bool on) { control()["auto"] = on; });
                else
                    add.skip();
                const JJson& own = have[k.key];
                auto shown = [&own](const char* field) { return own[field].isNumber() ? std::to_string(int(own[field].number())) : std::string(); };
                if (set)
                    add.integer(name + ":value", k.label, [control] { return int(std::as_const(control())["value"].number()); },
                                [control](int v) { control()["value"] = v; }, -1000000, 1000000);
                else
                    add.words(shown("value"));
                add.words(shown("min"));
                add.words(shown("max"));
                add.words(shown("default"));
                add.end();
            }
            add.note("Set? unticked: the camera keeps its own setting (shown greyed as it has it). The values are the camera's "
                     "own units; Min, Max and Default are the camera's, as it reports them while it runs. The settings ticked "
                     "are set each time the camera opens (OpenPnP's Freeze Properties).");
            add.button("reapplyControls", "Reapply to Camera", "Reapply the frozen properties to the camera.");
        }
    }

    // OpenPnP's Image Transforms: those its advanced calibration still applies (rotation,
    // offset, flips and scaling are the calibration's straightening here).
    add.tab("Image Transforms");
    add.group("Image Transforms");
    add.integer("cropWidth", "Crop Width", [c]() -> int& { return c().cropWidth; }, 0, 100000);
    add.tip("(Use 0 for no cropping)");
    add.integer("cropHeight", "Crop Height", [c]() -> int& { return c().cropHeight; }, 0, 100000);
    add.tip("(Use 0 for no cropping)");
    add.flag("deinterlace", "De-Interlace?", [c]() -> bool& { return c().deinterlace; });
    add.tip("(Removes interlacing from stacked frames)");
    add.note("Each picture is de-interlaced, then cut to the crop about its middle, before anything else is done with it: "
             "a camera is calibrated for the picture size it gives then. Rotation, offset, flipping and scaling are "
             "the calibration's straightening (As Taken off).");

    add.tab("White Balance");
    add.group("White Balance");
    add.header({ "Red", "Green", "Blue" });
    auto wb = [c]() -> JPCameraConfig::WhiteBalance& { return c().whiteBalance; };
    const char* channel[] = { "Red", "Green", "Blue" };
    for (const char* row : { "Balance", "Gamma" }) {
        const bool gamma = std::string(row) == "Gamma";
        add.row(row);
        for (size_t ch = 0; ch < 3; ++ch)
            // Set by hand, as OpenPnP's: the mapped balance (which they only approximate) dropped.
            add.number(std::string(gamma ? "gamma" : "balance") + channel[ch], std::string(channel[ch]) + " " + row,
                       [wb, ch, gamma] { return gamma ? wb().gamma[ch] : wb().balance[ch]; },
                       [wb, ch, gamma](double v) {
                           (gamma ? wb().gamma[ch] : wb().balance[ch]) = v;
                           for (auto& m : wb().maps) m.clear();
                       }, 3);
        add.end();
    }
    add.actions({ { "Overall", "whiteBalanceOverall" }, { "Brightest", "whiteBalanceBrightest" },
                  { "Mapped Roughly", "whiteBalanceMappedRoughly" }, { "Mapped Finely", "whiteBalanceMappedFinely" },
                  { "Reset", "whiteBalanceReset" } });
    add.note("Each channel is scaled by its balance, then given its gamma. Overall and Brightest work them out "
             "from what the camera sees now (something white or grey in view): the other channels brought up to "
             "the strongest, measured over the brighter fifth of the picture, or at its edge. Mapped Roughly (8 "
             "levels) and Mapped Finely (32) map each channel at each level of brightness, with a gradiented gray "
             "object in view; the balance and gamma then show roughly what the map does, and setting one by hand "
             "drops the map.");
    // OpenPnP's color balance graph: each channel's output for each input (one line, unbalanced).
    {
        const JPWhiteBalance table(wb());
        auto curve = std::make_shared<JPPlot>();
        curve->xTitle = "in";
        curve->yTitle = "out";
        if (wb().neutral()) {
            curve->series.push_back({ "neutral", JPPlot::Tone::Muted, { { 0, 0 }, { 255, 255 } } });
        } else {
            const char* names[] = { "red", "green", "blue" };
            const JPPlot::Tone tones[] = { JPPlot::Tone::First, JPPlot::Tone::Second, JPPlot::Tone::Third };
            for (size_t ch = 0; ch < 3; ++ch) {
                JPPlot::Series s { names[ch], tones[ch], {} };
                for (int i = 0; i < 256; ++i) s.points.push_back({ double(i), double(table.output(ch, i)) });
                curve->series.push_back(std::move(s));
            }
        }
        add.plot("Color Balance", curve);
    }

    add.tab("Position");
    coordinateSystem<JPCameraConfig>(add, cell, c, "(fixed to the machine)", true, f);
    if (c().mount.headId.empty()) {
        add.length("roamingRadius", "Roaming Radius", [c] { return c().roamingRadiusMm; },
                   [c](double v) { c().roamingRadiusMm = std::max(0.0, v); });
        add.tip("The maximum nominal roaming radius over the camera, which also indicates the largest part diagonal "
                "that can be supported. If set to zero, this switches off multi-shot vision (see package Vision "
                "Compositing). During bottom vision, the nozzle movement will be restricted, taking into consideration "
                "the distance of the nozzle from the camera center, and how much the part footprint is protruding from "
                "there (approximated by octogonal hull). Inside the roaming radius, the nozzle will also be freely moved "
                "at camera Z, i.e. without going to Safe Z. Note, this is the nominal radius, i.e. there must be extra "
                "space available for pick offsets and other deviations. Caution: the roaming radius is not enforced "
                "when jogging.");
    }

    add.tab("Advanced Calibration");
    // OpenPnP's General Settings: the picture's preparation (the same as Image
    // Transforms'), and the height of what the camera looks at.
    add.group("General Settings");
    add.flag("calDeinterlace", "Deinterlace", [c]() -> bool& { return c().deinterlace; });
    add.tip("Removes interlacing from stacked frames");
    add.integer("calCropWidth", "Cropped Width", [c]() -> int& { return c().cropWidth; }, 0, 100000);
    add.tip("(Use 0 for no cropping)");
    add.integer("calCropHeight", "Cropped Height", [c]() -> int& { return c().cropHeight; }, 0, 100000);
    add.tip("(Use 0 for no cropping)");
    if (c().mount.headId.empty()) {
        add.length("workingPlaneZ", "Default Working Plane Z", [c]() -> double& { return c().mount.offsetZ; });
        add.tip("This is the Z coordinate to which the bottom surface of parts carried by the nozzle will be lowered "
                "for visual alignment (the camera's Z, as on Position).");
    } else {
        add.length("workingPlaneZ", "Default Working Plane Z",
                   [c] {
                       if (c().workingPlaneZ) return *c().workingPlaneZ;
                       return c().calibrations.empty() ? 0.0 : c().calibrations.front().z;
                   },
                   [c](double v) { c().workingPlaneZ = v; });
        add.tip("This is the assumed Z coordinate of objects viewed by the camera if their true Z coordinate is "
                "unknown. Typically this is set to the Z coordinate of the working surface of the board(s) to be "
                "populated.");
        add.note("Calibrated at two heights, the camera's scale is taken at the Default Working Plane Z; until it "
                 "is set, at the height it was calibrated at (shown).");
    }
    add.group("Camera Calibration");
    add.actions({ { "Start Calibration", "calibrate" } });
    auto k = [c]() -> JPCameraConfig::Calibrating& { return c().calibrating; };
    const int most = JPCameraConfig::Calibrating::kMostPlaces;
    add.row("Places Across");
    add.integer("calColumns", "Places Across", [k]() -> int& { return k().columns; }, 3, most);
    add.integer("calRows", "Places Down", [k]() -> int& { return k().rows; }, 3, most);
    add.end();
    add.number("calReach", "Reach (share)", [k] { return k().reach; },
               [k](double v) { k().reach = std::clamp(v, 0.1, 1.0); }, 2);
    add.number("calOutlier", "Outlier Limit (x spread)", [k] { return k().outlierSpread; },
               [k](double v) { k().outlierSpread = std::max(1.0, v); }, 1);
    add.number("calMaxRms", "Worst Fit Taken (px)", [k] { return k().maxRmsPx; },
               [k](double v) { if (v > 0) k().maxRmsPx = v; }, 2);
    add.row("Lead-in");
    add.length("calLeadIn", "Lead-in", [k] { return k().leadInMm; }, [k](double v) { if (v >= 0) k().leadInMm = v; }, 2);
    add.integer("calFrames", "Pictures Each", [k]() -> int& { return k().frames; }, 1, JPCameraConfig::Calibrating::kMostFrames);
    add.end();
    add.flag("calTwoHeights", "Two Heights?", [k]() -> bool& { return k().twoHeights; });
    if (c().mount.headId.empty())
        add.length("calRaise", "Raise For The Second", [k] { return k().raiseMm; },
                   [k](double v) { if (v > 0) k().raiseMm = v; }, 2);
    add.note("Calibrating moves the mark through a grid of places across the picture, Reach of the way from the "
             "middle to as near the edge as leaves room for the mark (1: all the way). More places measure the "
             "lens better and take longer. A measurement further from the fit than Outlier Limit times the fit's "
             "spread is left out, one in ten at most; a fit whose spread is worse than Worst Fit Taken is refused. "
             "Each place is come to the same way, from Lead-in back along both axes, so the drives' play is taken "
             "up alike every time, and found in Pictures Each pictures, their mean. "
             "Two Heights? measures it again at another height: a camera on a head over the head's secondary "
             "calibration mark (Machine Setup, the head), a fixed one with the nozzle's tip raised by Raise For The "
             "Second. How the scale changes with height gives where the camera is and its field of view.");
    // Straightened, a wide lens's picture no longer fills a rectangle.
    add.row("Crop All Invalid Pixels");
    add.integer("showAll", "Crop All Invalid Pixels", [c] { return int(std::lround(c().showAll * 100)); },
                [c](int v) { c().showAll = std::clamp(v, 0, 100) / 100.0; }, 0, 100);
    add.end();
    add.note("0 crops every pixel the straightening leaves without picture; 100 shows all of the picture, "
             "dark corners and all.");
    for (const JPCameraCalibration& cal : c().calibrations) calibrationResults(add, cal, c().looksUp,
                                                                         c().mount.headId.empty() ? c().mount.offsetZ
                                                                         : c().workingPlaneZ     ? *c().workingPlaneZ
                                                                                                 : cal.z,
                                                                         !c().mount.headId.empty());

    // OpenPnP's AutoFocusProvider wizard, for a fixed camera that senses focus.
    if (c().mount.headId.empty() && c().focusSensingMethod == "AutoFocus") {
        add.tab("Auto Focus");
        add.group("General");
        auto af = [c]() -> JPCameraConfig::AutoFocus& { return c().autoFocus; };
        add.row("Focal Resolution");
        add.length("focalResolution", "Focal Resolution", [af] { return af().focalResolutionMm; },
                   [af](double v) { if (v > 0) af().focalResolutionMm = v; });
        add.iconButton("autoFocusTest", "position-actuator", "Auto-Focus the selected nozzle in this camera. If a part is on the nozzle, "
                                                      "its height will be determined.");
        add.end();
        add.tip("The focal resolution at which to stop the search. The smaller, the more precise the focus, and the longer it takes.");
        add.integer("averagedFrames", "Averaged Frames", [af]() -> int& { return af().averagedFrames; }, 1, 100);
        add.tip("Number of frames to average when determining the focus score. Increase to filter out noise.");
        add.number("focusSpeed", "Focus Speed", [af] { return af().focusSpeed; },
                   [af](double v) { af().focusSpeed = std::clamp(v, 0.01, 1.0); });
        add.tip("Speed factor when moving through the focal range. Slower moves avoid vibrations.");
        add.flag("showDiagnostics", "Show Diagnostics?", [af]() -> bool& { return af().showDiagnostics; });
        add.tip("Show detected edges and Auto Focus status text in the camera view.");
        add.row("Last Focus Distance");
        add.text("lastFocusDistance", "Last Focus Distance",
                 std::function<std::string()>([c] {
                     if (!c().lastFocusDistanceMm) return std::string();
                     char text[32];
                     std::snprintf(text, sizeof text, "%.3f", *c().lastFocusDistanceMm);
                     return std::string(text);
                 }),
                 nullptr);
        add.button("adjustCameraZ", "Adjust Camera Z",
                   "After having auto-focused, adjust the camera Z coordinate to match the focal distance i.e. make sure the "
                   "camera is focused.");
        add.end();
        add.note("Test runs the nozzle chosen on the Jog panel over the camera from its tip's largest part height down to the "
                 "camera's Z and finds where it is in focus; Last Focus Distance is how far above the camera's Z that was.");
    }
}

// OpenPnP's ReferenceActuatorProfilesWizard: the actuators a profile sets, and
// the profiles (a name, Default ON, Default OFF, and a value for each).
void actuatorProfilesTab(JPFormBuilder& add, JPCellConfig& cell, std::function<JPActuatorConfig&()> a, JPSetupProperties::Form& f) {
    using VT = JPActuatorConfig::ValueType;
    constexpr size_t kN = JPActuatorConfig::kProfileActuators;
    add.tab("Profiles");
    add.group("Actuators");
    JPFormBuilder::Named others;
    others.add("(none)", "");
    for (const JPActuatorConfig& o : cell.actuators)
        if (o.id != a().id) others.add(o.name.empty() ? o.id : o.name, o.id);
    for (size_t k = 0; k < kN; ++k) {
        const std::string name = "profileActuator" + std::to_string(k + 1);
        add.byName(name, "Actuator " + std::to_string(k + 1), others, [a, k]() -> std::string& { return a().profileActuators[k]; });
        f.reshaping.push_back(name);
    }
    add.group("Profiles");
    // The columns: the actuators set, by name.
    std::vector<size_t> used;
    JPFormBuilder::Strings titles { "Name", "Default ON", "Default OFF" };
    auto member = [&cell](const std::string& id) -> const JPActuatorConfig* {
        for (const JPActuatorConfig& o : cell.actuators)
            if (o.id == id) return &o;
        return nullptr;
    };
    for (size_t k = 0; k < kN; ++k)
        if (const JPActuatorConfig* m = member(a().profileActuators[k])) {
            used.push_back(k);
            titles.push_back(m->name);
        }
    titles.push_back("");
    add.header(titles);
    for (size_t i = 0; i < a().profiles.size(); ++i) {
        const std::string at = "profile:" + std::to_string(i) + ":";
        auto p = [a, i]() -> JPActuatorConfig::Profile& { return a().profiles[i]; };
        add.row("");
        add.text(at + "name", "Name", [p] { return p().name; }, [p](const std::string& v) {
            // Kept trimmed, as OpenPnP's.
            const size_t b = v.find_first_not_of(" \t"), e = v.find_last_not_of(" \t");
            p().name = b == std::string::npos ? std::string() : v.substr(b, e - b + 1);
        });
        // Each default for one profile only, and a profile not both.
        add.flag(at + "defaultOn", "Default ON", [p] { return p().defaultOn; }, [a, p](bool on) {
            if (on) for (auto& q : a().profiles) q.defaultOn = false;
            p().defaultOn = on;
            if (on) p().defaultOff = false;
        });
        add.flag(at + "defaultOff", "Default OFF", [p] { return p().defaultOff; }, [a, p](bool on) {
            if (on) for (auto& q : a().profiles) q.defaultOff = false;
            p().defaultOff = on;
            if (on) p().defaultOn = false;
        });
        f.reshaping.push_back(at + "defaultOn");
        f.reshaping.push_back(at + "defaultOff");
        for (size_t k : used) {
            const JPActuatorConfig* m = member(a().profileActuators[k]);
            const std::string name = at + "value" + std::to_string(k + 1);
            auto get = [p, k] { return p().values[k]; };
            auto set = [p, k](const std::string& v) { p().values[k] = v; };
            // A switch's value true or false, a profile actuator's one of its profiles, else as typed; empty leaves it.
            if (m->valueType == VT::Boolean) {
                add.choice(name, m->name, { "", "true", "false" }, get, set);
            } else if (m->valueType == VT::Profile) {
                JPFormBuilder::Strings names { "" };
                for (const JPActuatorConfig::Profile& q : m->profiles)
                    if (!q.name.empty()) names.push_back(q.name);
                add.choice(name, m->name, names, get, set);
            } else {
                add.text(name, m->name, get, set);
            }
        }
        add.editButton(at + "delete", "Delete", "Delete Profile", [a, i] {
            if (i < a().profiles.size()) a().profiles.erase(a().profiles.begin() + long(i));
        });
        add.end();
    }
    add.endColumns();
    add.editButton("profile:add", "Add Profile", "Add Profile", [a] { a().profiles.push_back({}); });
    add.note("Set to a profile (the Actuators panel), each actuator is set to its value in it; one left empty is "
             "left as it is. Switched on or off, the actuator takes its Default ON or Default OFF profile.");
}

// OpenPnP's ActuatorInterlockMonitorConfigurationWizard.
void actuatorInterlockTab(JPFormBuilder& add, JPCellConfig& cell, std::function<JPActuatorConfig&()> a, JPSetupProperties::Form& f) {
    using IL = JPActuatorConfig::Interlock;
    auto il = [a]() -> IL& { return a().interlock; };
    add.tab("Axis Interlock");
    add.group("Axis Interlock");
    add.choice("interlockType", "Interlock Type", IL::types(), [il] { return il().type; }, [il](const std::string& v) { il().type = v; });
    f.reshaping.push_back("interlockType");
    const std::string& type = il().type;
    if (type.find("InRange") != std::string::npos) {
        add.row("Confirmation range");
        add.number("goodMin", "Min", [il]() -> double& { return il().goodMin; });
        add.number("goodMax", "Max", [il]() -> double& { return il().goodMax; });
        add.end();
    }
    if (type.find("Match") != std::string::npos) {
        add.text("pattern", "Confirmation pattern", [il]() -> std::string& { return il().pattern; }, "long");
        add.flag("byRegex", "Regex?", [il]() -> bool& { return il().byRegex; });
    }
    for (size_t k = 0; k < 4; ++k)
        add.byName("interlockAxis" + std::to_string(k + 1), "Axis " + std::to_string(k + 1), named(cell.axes, "(none)"),
                   [il, k]() -> std::string& { return il().axes[k]; });
    add.group("Interlock Conditions");
    JPFormBuilder::Named others;
    others.add("(none)", "");
    for (const JPActuatorConfig& o : cell.actuators)
        if (o.id != a().id && o.valueType == JPActuatorConfig::ValueType::Boolean) others.add(o.name.empty() ? o.id : o.name, o.id);
    add.row("Boolean Actuator");
    add.byName("conditionalActuator", "Boolean Actuator", others, [il]() -> std::string& { return il().conditionalActuatorId; });
    add.choice("conditionalState", "State", IL::states(), [il] { return il().conditionalState; },
               [il](const std::string& v) { il().conditionalState = v; });
    add.end();
    add.row("Speed [%]");
    add.number("speedMin", "Min", [il] { return il().speedMin * 100; }, [il](double v) { il().speedMin = v / 100; }, 1);
    add.number("speedMax", "Max", [il] { return il().speedMax * 100; }, [il](double v) { il().speedMax = v / 100; }, 1);
    add.end();
    add.note("When any of its axes moves: the Signal types switch it (on while moving, standing still, inside or outside "
             "the axes' safe zone, parked or not; switched before the move when that is leaving, after it when coming), "
             "the Confirm types read it before or after the move and stop the machine unless it reads in range or matches. "
             "Only while the Boolean Actuator is in its state (Just: and changed since last time; Unknown: or never "
             "switched) and at a machine speed in range.");
}

void actuatorForm(JPCellConfig& cell, const std::string& id, JPSetupProperties::Form& f) {
    auto a = finder(cell.actuators, id);
    f.title = "Actuator " + a().name;
    JPFormBuilder add(f);
    add.tab("Configuration");
    add.group("Properties");
    add.byName("driver", "Driver", named(cell.drivers, "(none)"), [a]() -> std::string& { return a().driverId; });
    add.text("name", "Name", [a]() -> std::string& { return a().name; }, "name");
    if (a().neoden4Feeder.on) {
        // OpenPnP's NeoDen4FeederActuator: the feeder and peeler it works, and
        // how hard; Change Feeder ID gives the feeder (kept in it) a new one.
        auto nf = [a]() -> JPActuatorConfig::Neoden4Feeder& { return a().neoden4Feeder; };
        constexpr int kMostId = 99, kMostStrength = 255;
        add.integer("peelerId", "Peeler ID", [nf]() -> int& { return nf().peelerId; }, 0, kMostId);
        add.integer("feederId", "Feeder ID", [nf]() -> int& { return nf().feederId; }, 0, kMostId);
        add.integer("feedStrength", "Feed Strength", [nf]() -> int& { return nf().feedStrength; }, 0, kMostStrength);
        add.integer("peelStrength", "Peel Strength", [nf]() -> int& { return nf().peelStrength; }, 0, kMostStrength);
        add.integer("peelLength", "Peel length [%]", [nf]() -> int& { return nf().peelLength; }, 0, 1000);
        add.tip("How far the peeler peels, as a share of five times the length fed.");
        add.row("Change Feeder ID");
        Strings ids;
        for (int i = 0; i <= kMostId; ++i) ids.push_back(std::to_string(i));
        add.choice("newFeederId", "New ID", ids, [] { return std::to_string(JPSetupProperties::neoden4NewFeederId()); },
                   [](const std::string& v) { JPSetupProperties::neoden4NewFeederId() = std::atoi(v.c_str()); });
        add.button("changeFeederId", "Change", "Give the feeder its Feeder ID names the New ID (stored in the feeder's NVMEM).");
        add.end();
        f.viewOnly.push_back("newFeederId");
    }
    add.group("Coordinate System");
    add.byName("head", "Head", named(cell.heads, "(on the machine)"), [a]() -> std::string& { return a().mount.headId; });
    f.reshaping.push_back("head");
    add.flag("axisInterlock", "Axis Interlock?", [a]() -> bool& { return a().interlock.enabled; });
    add.tip("Enable to get an extra Wizard tab to configure an Axis Interlocking Actuator");
    f.reshaping.push_back("axisInterlock");
    // OpenPnP's Machine Coordination: waiting for the machine around actuating and reading it.
    add.group("Machine Coordination");
    add.choice("coordinatedBeforeActuate", "Before Actuation?", { "None", "CommandStillstand", "WaitForStillstand" },
               [a] { return a().coordinatedBeforeActuate; }, [a](const std::string& v) { a().coordinatedBeforeActuate = v; });
    add.tip("Coordinate with the machine, before the actuator is actuated, i.e. wait for the controllers to acknowledge "
            "that all the pending commands (including motion) were sent and executed.");
    add.choice("coordinatedAfterActuate", "After Actuation?", { "None", "WaitForUnconditionalCoordination" },
               [a] { return a().coordinatedAfterActuate; }, [a](const std::string& v) { a().coordinatedAfterActuate = v; });
    add.tip("Coordinate with the machine, after the actuator was actuated, i.e. wait for the controllers to acknowledge "
            "that the actuation as well as all the pending commands (including motion) were sent and executed and any "
            "position report processed.");
    add.choice("coordinatedBeforeRead", "Before Read?", { "None", "WaitForStillstand" },
               [a] { return a().coordinatedBeforeRead; }, [a](const std::string& v) { a().coordinatedBeforeRead = v; });
    add.tip("Coordinate with the machine, before the actuator is read, i.e. wait for the controllers to acknowledge that "
            "all the pending commands (including motion) were sent and executed.");
    add.group("General");
    // As the machine's state changes: once connected, once homed, before it is let go.
    add.header({ "Enabled", "Homed", "Disabled" });
    add.row("Actuation");
    add.choice("enabledActuation", "Enabled", { "AssumeUnknown", "ActuateOn", "ActuateOff" },
               [a] { return a().enabledActuation; }, [a](const std::string& v) { a().enabledActuation = v; });
    add.choice("homedActuation", "Homed", { "LeaveAsIs", "ActuateOn", "ActuateOff" },
               [a] { return a().homedActuation; }, [a](const std::string& v) { a().homedActuation = v; });
    add.choice("disabledActuation", "Disabled", { "LeaveAsIs", "ActuateOn", "ActuateOff" },
               [a] { return a().disabledActuation; }, [a](const std::string& v) { a().disabledActuation = v; });
    add.end();
    // {index} in a command is replaced by the index.
    add.text("index", "Index", [a]() -> std::string& { return a().index; });
    add.text("unit", "Unit Read", [a]() -> std::string& { return a().unit; });
    // OpenPnP's names for the value types.
    using VT = JPActuatorConfig::ValueType;
    add.choice("valueType", "Value Type", { "Boolean", "Double", "String", "Profile" },
               [a] {
                   return std::string(a().valueType == VT::Number ? "Double" : a().valueType == VT::Text ? "String"
                                    : a().valueType == VT::Profile ? "Profile" : "Boolean");
               },
               [a](const std::string& v) {
                   a().valueType = v == "Double" ? VT::Number : v == "String" ? VT::Text : v == "Profile" ? VT::Profile : VT::Boolean;
               });
    f.reshaping.push_back("valueType");
    if (!a().scriptName.empty()) {
        // OpenPnP's ScriptActuatorConfigurationWizard.
        add.group("Script");
        add.text("scriptName", "Script Name", [a]() -> std::string& { return a().scriptName; }, "long");
        add.tip("The script, in the scripts folder, run to actuate it.");
        add.note("Switched, it is told actuateBoolean (true or false); set, actuateDouble (a Double one) or actuateString; "
                 "and its own name as actuator, in JPLACER_GLOBALS (see the Scripts menu).");
        if (a().interlock.enabled) actuatorInterlockTab(add, cell, a, f);
        return;
    }
    if (a().http.on) {
        // OpenPnP's HttpActuatorConfigurationWizard.
        add.group("HTTP");
        add.text("onUrl", "On URL", [a]() -> std::string& { return a().http.onUrl; }, "long");
        add.text("offUrl", "Off URL", [a]() -> std::string& { return a().http.offUrl; }, "long");
        add.text("paramUrl", "Param URL", [a]() -> std::string& { return a().http.paramUrl; }, "long");
        add.text("readUrl", "Read URL", [a]() -> std::string& { return a().http.readUrl; }, "long");
        add.text("regex", "Read Regex", [a]() -> std::string& { return a().http.regex; }, "long");
        add.note("Switched by a GET of the On or Off URL (with none, the Param URL with 1 or 0), set by the Param URL "
                 "with {val} the value, read by the Read URL: its lines, or each one's (?<Value>...) group of the regex. "
                 "A URL the same as the one asked last is not asked again.");
        if (a().valueType == VT::Profile) actuatorProfilesTab(add, cell, a, f);
        if (a().interlock.enabled) actuatorInterlockTab(add, cell, a, f);
        return;
    }
    add.group("Commands");
    add.text("onCommand", "On", [a]() -> std::string& { return a().onCommand; }, "long");
    add.text("offCommand", "Off", [a]() -> std::string& { return a().offCommand; }, "long");
    if (a().valueType != VT::Boolean && a().valueType != VT::Profile) {
        add.text("valueCommand", "Set Value", [a]() -> std::string& { return a().valueCommand; }, "long");
        add.row("On / Off Values");
        add.text("onValue", "On Value", [a]() -> std::string& { return a().onValue; });
        add.text("offValue", "Off Value", [a]() -> std::string& { return a().offValue; });
        add.end();
    }
    add.text("readCommand", "Read", [a]() -> std::string& { return a().readCommand; }, "long");
    add.text("readPattern", "Read Reply Pattern", [a]() -> std::string& { return a().readPattern; }, "long");
    add.note(a().valueType == VT::Boolean
                 ? "{index} in a command is replaced by the index."
                 : "{index} in a command is replaced by the index, {value} in Set Value by the value it is set to "
                   "(the Actuators panel's box). Without commands of their own, On and Off set it to the On and "
                   "Off Values.");
    if (a().valueType == VT::Profile) actuatorProfilesTab(add, cell, a, f);
    if (a().interlock.enabled) actuatorInterlockTab(add, cell, a, f);
    if (a().thermistor.on) {
        // OpenPnP's ThermistorToLinearSensorActuatorTransforms.
        using T = JPActuatorConfig::Thermistor;
        auto value = [a](double T::*field) { return [a, field]() -> double& { return a().thermistor.*field; }; };
        constexpr int kCoefficientDecimals = 18;   // OpenPnP's coefficients, to their last digit
        add.tab("Transforms");
        add.group("Thermistor");
        add.number("thermistorA", "A", value(&T::a), kCoefficientDecimals);
        add.number("thermistorB", "B", value(&T::b), kCoefficientDecimals);
        add.number("thermistorC", "C", value(&T::c), kCoefficientDecimals);
        add.number("thermistorR1", "R1", value(&T::r1));
        add.tip("(Not yet supported)");
        add.number("thermistorR2", "R2", value(&T::r2));
        add.group("ADC");
        add.number("adcMax", "Maximum Value", value(&T::adcMax));
        add.number("vRef", "Voltage Reference", value(&T::vRef));
        add.group("Linear Transform");
        add.number("thermistorScale", "Scale", value(&T::scale));
        add.number("thermistorOffset", "Offset", value(&T::offset));
        add.note("What is read is taken as a temperature (degrees C) and turned into what a linear sensor would read: "
                 "the thermistor's resistance at it (A, B, C), below R2 in a divider read by the ADC, as a voltage, "
                 "times Scale plus Offset.");
    }
}

// OpenPnP's SoundSignalerConfigurationWizard and ActuatorSignalerConfigurationWizard.
void signalerForm(JPCellConfig& cell, const std::string& id, JPSetupProperties::Form& f) {
    auto s = finder(cell.signalers, id);
    f.title = s().className() + " " + s().name;
    JPFormBuilder add(f);
    add.tab("Configuration");
    add.group("");
    if (s().kind == JPSignalerConfig::Kind::Sound) {
        add.flag("errorSound", "Play sound on error?", [s]() -> bool& { return s().errorSound; });
        add.flag("finishedSound", "Play sound on completion?", [s]() -> bool& { return s().finishedSound; });
        return;
    }
    if (s().kind == JPSignalerConfig::Kind::Neoden4) {
        // OpenPnP's Neoden4SignalerConfigurationWizard: each sound, and a test of it.
        add.row("Play sound on error?");
        add.flag("errorSound", "Play sound on error?", [s]() -> bool& { return s().errorSound; });
        add.button("testErrorSound", "Test error sound", "Beep the NeoDen 4's buzzer as for a job error.");
        add.end();
        add.row("Play sound on completion?");
        add.flag("finishedSound", "Play sound on completion?", [s]() -> bool& { return s().finishedSound; });
        add.button("testFinishedSound", "Test finished sound", "Beep the NeoDen 4's buzzer as for a job finished.");
        add.end();
        return;
    }
    add.byName("actuator", "Actuator", named(cell.actuators, "(none)"), [s]() -> std::string& { return s().actuatorId; });
    std::vector<std::string> states { "" };
    for (const std::string& k : JPSignalerConfig::jobStateKeys()) states.push_back(k);
    add.choice("jobState", "Job State", states,
               [s] { return s().jobState ? JPSignalerConfig::jobStateKeys()[size_t(*s().jobState)] : std::string(); },
               [s](const std::string& v) {
                   const auto& keys = JPSignalerConfig::jobStateKeys();
                   const auto it = std::find(keys.begin(), keys.end(), v);
                   s().jobState = it == keys.end() ? std::nullopt
                                                   : std::optional(JPSignalerConfig::JobState(it - keys.begin()));
               });
}

// OpenPnP's ReferencePnpJobProcessorConfigurationWizard: its General group.
void jobProcessorForm(JPCellConfig& cell, JPSetupProperties::Form& f) {
    f.title = "ReferencePnpJobProcessor";
    JPFormBuilder add(f);
    add.tab("Configuration");
    add.group("General");
    JPJobProcessorConfig& j = cell.jobProcessor;
    add.integer("maxPlacementRetries", "Max Placement Attempts", [&j]() -> int& { return j.maxPlacementRetries; }, 0, 1000);
    add.tip("The number of attempts at the whole feed/pick/align/place process for each placement. The part is discarded "
            "after a failed attempt. Retries may not be consecutive because each attempt is independent");
    auto byIndex = [](const std::vector<std::string>& names, auto& field) {
        return std::pair { [&names, &field] { return names[size_t(field)]; },
                           [&names, &field](const std::string& v) {
                               for (size_t i = 0; i < names.size(); ++i)
                                   if (names[i] == v) field = std::remove_reference_t<decltype(field)>(i);
                           } };
    };
    auto [orderGet, orderSet] = byIndex(JPJobProcessorConfig::jobOrderNames(), j.jobOrder);
    add.choice("jobOrder", "Job order", JPJobProcessorConfig::jobOrderNames(), orderGet, orderSet);
    add.tip("All placements of a job will be sorted using this order. However, the actual order may differ because parts "
            "that can be placed using the currently loaded nozzle tip(s) will take precedence.");
    auto [strategyGet, strategySet] = byIndex(JPJobProcessorConfig::strategyNames(), j.strategy);
    add.choice("strategy", "Nozzle tip loading strategy", JPJobProcessorConfig::strategyNames(), strategyGet, strategySet);
    add.tip("JobPlanner strategy to trade nozzle tip changes vs requested placement ordering.");
    add.integer("maxVisionRetries", "Max Vision Attempts", [&j]() -> int& { return j.maxVisionRetries; }, 0, 1000);
    add.tip("The number of attempts at vision alignment, in cases where the first attempt raises an error.");
    add.flag("steppingToNextMotion", "Step Next Motion", [&j]() -> bool& { return j.steppingToNextMotion; });
    add.tip("Stepping will only stop at the next step with motion");
    add.flag("optimizeMultipleNozzles", "Optimize Multiple Nozzles", [&j]() -> bool& { return j.optimizeMultipleNozzles; });
    add.tip("Optimize the path of Pick, Align and Place steps for multi nozzle machines by changing the order nozzles are "
            "handled.");
    add.flag("preRotateAllNozzles", "Pre-Rotate All Nozzles", [&j]() -> bool& { return j.preRotateAllNozzles; });
    add.tip("Pre-rotate all nozzles on the move to the first feed or pick location, the bottom camera and the first place "
            "location.");
    add.integer("feederFaultLimit", "Feeder fault limit", [&j]() -> int& { return j.feederFaultLimit; }, 0, 1000);
    add.tip("When using deferred errors, a feeder can be automatically disabled if the parts obtained from that feeder "
            "cause problems. This is the number of errors when a feeder is disabled");
    add.integer("feederFaultWindowSize", "Feeder fault window size", [&j]() -> int& { return j.feederFaultWindowSize; }, 1, 1000);
    add.tip("When using deferred errors, a feeder can be automatically disabled if the parts obtained from that feeder "
            "cause problems. This is the number of recent placements over which it will count the error");
}

// The vision settings of a kind, by name (the one set always among them).
JPFormBuilder::Named visionChoices(const JPConfiguration* config, JPVisionSettings::Kind kind, const std::string& set) {
    JPFormBuilder::Named n;
    bool has = false;
    if (config)
        for (const JPVisionSettings& v : config->visionSettings())
            if (v.kind == kind) {
                n.add(v.name, v.id);
                has = has || v.id == set;
            }
    if (!has) n.add(set, set);
    return n;
}

// How jplacer finds it: its own finder, or the vision settings' OpenPnP pipeline.
void finder(JPFormBuilder& add, bool& pipeline, const char* what) {
    const JPFormBuilder::Strings names { "jplacer", "Pipeline" };
    add.choice("finder", std::string("Find ") + what + " with", names, [&pipeline] { return std::string(pipeline ? "Pipeline" : "jplacer"); },
               [&pipeline](const std::string& v) { pipeline = v == "Pipeline"; });
    add.tip(std::string("jplacer: jplacer's own finder, which needs no tuning. Pipeline: the vision settings' OpenPnP pipeline, "
                        "tuned with its sliders and the Pipeline Editor (as OpenPnP finds ") + what + ").");
}

// OpenPnP's ReferenceBottomVisionConfigurationWizard.
// The machine's default vision settings of a kind, their page as a second tab (OpenPnP's
// BottomVisionSettingsConfigurationWizard, FiducialVisionSettingsConfigurationWizard), edited in the configuration.
// (Its page is its own tab, "Bottom Vision Settings" or "Fiducial Vision Settings".)
void defaultSettingsTab(JPFormBuilder& add, JPConfiguration* config, const std::string& id, bool bottom, const JPVisionTests* tests) {
    if (!config) return;
    const JPVisionSettings* v = config->visionSettings(id);
    if (!v) return;
    std::string used;
    for (const std::string& u : config->visionUsedIn(*v, id, bottom ? "Bottom Vision" : "Fiducal Locator"))
        used += (used.empty() ? "" : ", ") + u;
    JPVisionForms::addPage(add, *config, id, used, JPVisionForms::Holder {}, tests);
}

void bottomVisionForm(JPCellConfig& cell, JPSetupProperties::Form& f, JPConfiguration* config, const JPVisionTests* tests) {
    f.title = "ReferenceBottomVision";
    JPFormBuilder add(f);
    add.tab("ReferenceBottomVision");
    add.group("General");
    JPVisionConfig& v = cell.vision;
    add.flag("bottomVisionEnabled", "Enabled?", [&v]() -> bool& { return v.bottomVisionEnabled; });
    add.byName("bottomVisionId", "Bottom Vision Settings", visionChoices(config, JPVisionSettings::Kind::Bottom, v.bottomVisionId),
               [&v]() -> std::string& { return v.bottomVisionId; });
    add.flag("preRotate", "Rotate parts prior to vision?", [&v]() -> bool& { return v.preRotate; });
    add.tip("Pre-rotate default setting for bottom vision. Can be overridden on individual parts.");
    add.integer("maxVisionPasses", "Max. vision passes", [&v]() -> int& { return v.maxVisionPasses; }, 1, 100);
    add.tip("The maximum number of bottom vision passes performed to get a good fix on the part.");
    add.length("maxLinearOffsetMm", "Max. linear offset", [&v]() -> double& { return v.maxLinearOffsetMm; });
    add.tip("The maximum linear part offset accepted as a good fix i.e. where no additional vision pass is needed.");
    add.number("maxAngularOffset", "Max. angular offset", [&v]() -> double& { return v.maxAngularOffset; });
    add.tip("The maximum angular part offset accepted as a good fix i.e. where no additional vision pass is needed.");
    finder(add, v.bottomPipeline, "parts");
    defaultSettingsTab(add, config, v.bottomVisionId, true, tests);
}

// OpenPnP's ReferenceFiducialLocatorConfigurationWizard.
void fiducialLocatorForm(JPCellConfig& cell, JPSetupProperties::Form& f, JPConfiguration* config, const JPVisionTests* tests) {
    f.title = "ReferenceFiducialLocator";
    JPFormBuilder add(f);
    add.tab("ReferenceFiducialLocator");
    add.group("General");
    JPVisionConfig& v = cell.vision;
    add.byName("fiducialVisionId", "Vision Settings", visionChoices(config, JPVisionSettings::Kind::Fiducial, v.fiducialVisionId),
               [&v]() -> std::string& { return v.fiducialVisionId; });
    add.flag("enabledAveraging", "Average Matches?", [&v]() -> bool& { return v.enabledAveraging; });
    add.tip("Finally calculates the arithmetic average over all matches (except the first). Needs 3 or more repeated "
            "recognitions to work.");
    add.length("fiducialMaxDistanceMm", "Max. Distance (old pipelines only)", [&v]() -> double& { return v.fiducialMaxDistanceMm; });
    add.tip("Maximum allowed distance between nominal fiducial location and detected location. This only applies where the "
            "vision pipeline does not have a maxDistance stage.");
    finder(add, v.fiducialPipeline, "fiducials");
    defaultSettingsTab(add, config, v.fiducialVisionId, false, tests);
}

} // namespace

int& JPSetupProperties::neoden4NewFeederId() {
    static int id = 0;
    return id;
}

JPSetupProperties::Form JPSetupProperties::forNode(JPCellConfig& cell, const std::string& path, const std::vector<JPFirmwareProfile>& profiles,
                                                   JPConfiguration* config, const JPVisionTests* tests,
                                                   const JPMotionTestResult* motionTest, const Live& live) {
    Form f;
    const JPSetupTree::Path p = JPSetupTree::parse(path);
    if (p.kind == "machine") machineForm(cell, f, motionTest);
    else if (p.kind == "jobprocessor") jobProcessorForm(cell, f);
    else if (p.kind == "vision" && p.id == "bottom") bottomVisionForm(cell, f, config, tests);
    else if (p.kind == "vision" && p.id == "fiducial") fiducialLocatorForm(cell, f, config, tests);
    else if (p.kind == "driver" && has(cell.drivers, p.id)) driverForm(cell, p.id, profiles, f);
    else if (p.kind == "axis" && has(cell.axes, p.id)) axisForm(cell, p.id, f);
    else if (p.kind == "head" && has(cell.heads, p.id)) headForm(cell, p.id, f);
    else if (p.kind == "nozzle" && has(cell.nozzles, p.id)) nozzleForm(cell, p.id, f);
    else if (p.kind == "nozzletip" && has(cell.nozzleTips, p.id)) nozzleTipForm(cell, p.id, f, live);
    else if (p.kind == "step" && has(cell.nozzleTips, p.owner)) stepForm(cell, p, f);
    else if (p.kind == "camera" && has(cell.cameras, p.id)) cameraForm(cell, p.id, f, live);
    else if (p.kind == "actuator" && has(cell.actuators, p.id)) actuatorForm(cell, p.id, f);
    else if (p.kind == "signaler" && has(cell.signalers, p.id)) signalerForm(cell, p.id, f);
    return f;
}

} // inline namespace jf
