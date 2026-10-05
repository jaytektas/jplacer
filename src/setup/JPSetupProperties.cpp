// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPSetupProperties.h"

#include "JPVisionForms.h"

#include "JPFormBuilder.h"
#include "JPSetupTree.h"

#include "machine/JPTcpLink.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <memory>
#include <optional>
#include <utility>

inline namespace jf {

namespace {

using Strings = std::vector<std::string>;

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
        add.number("offsetX", "X", [mount]() -> double& { return mount().offsetX; });
        add.number("offsetY", "Y", [mount]() -> double& { return mount().offsetY; });
        add.number("offsetZ", "Z", [mount]() -> double& { return mount().offsetZ; });
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
    add.number("offsetX", "Offset X", [mount]() -> double& { return mount().offsetX; });
    add.number("offsetY", "Offset Y", [mount]() -> double& { return mount().offsetY; });
    add.number("offsetZ", "Offset Z", [mount]() -> double& { return mount().offsetZ; });
    add.end();
}

void machineForm(JPCellConfig& cell, JPSetupProperties::Form& f) {
    f.title = "Machine";
    JPFormBuilder add(f);
    add.tab("Configuration");
    add.group("General");
    add.text("name", "Name", [&cell]() -> std::string& { return cell.name; }, "name");
    add.flag("parkAfterHome", "Park after homed?", [&cell]() -> bool& { return cell.parkAfterHome; });
    add.flag("safeZPark", "Park all at Safe Z?", [&cell]() -> bool& { return cell.safeZPark; });
    add.tip("When the Z Park button is pressed, move all tools mounted on the same head to safe Z.");
    add.number("unsafeZRoaming", "Unsafe Z Roaming", [&cell]() -> double& { return cell.unsafeZRoamingMm; }, 2);
    add.tip("Maximum allowable roaming distance at unsafe Z. Virtual Z axes (typically on cameras) are invisible, therefore "
            "it can easily be overlooked that you are at unsafe Z. Jogging further away will automatically move the "
            "virtual axis to Safe Z.");
    add.flag("autoToolSelect", "Auto tool select?", [&cell]() -> bool& { return cell.autoToolSelect; });
    add.tip("Whenever an explicit user action is performed on a tool, automatically select it in Machine Controls.");
    add.flag("autoLoadMostRecentJob", "Auto-load most recent job?", [&cell]() -> bool& { return cell.autoLoadMostRecentJob; });
    add.group("Locations");
    add.header({ "X", "Y", "Z", "Rotation", "Set?" });
    auto at = [&cell]() -> std::optional<JPMachineLocation>& { return cell.discardLocation; };
    add.row("Discard Location", at() ? Place::Location : Place::None);
    if (at()) {
        add.number("discardX", "Discard X", [at]() -> double& { return at()->x; });
        add.number("discardY", "Discard Y", [at]() -> double& { return at()->y; });
        add.number("discardZ", "Discard Z", [at]() -> double& { return at()->z; });
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
    add.number("defaultBoardX", "Default Board X", [board]() -> double& { return board().x; });
    add.number("defaultBoardY", "Default Board Y", [board]() -> double& { return board().y; });
    add.number("defaultBoardZ", "Default Board Z", [board]() -> double& { return board().z; });
    add.number("defaultBoardRotation", "Default Board Rotation", [board]() -> double& { return board().rotation; });
    add.skip();   // always set: nothing under Set?
    add.end();
    add.note("Discard Location: where a nozzle drops a part that is not wanted. Default Board Location: where a "
             "board or panel added to a job starts.");
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
    // A simulated controller is for trying jplacer without a machine; it is
    // set up in the cell file.
    if (std::as_const(d().link)["type"].str() == "simulated") {
        add.group("Communications");
        add.text("link", "Link", [] { return std::string("simulated (set up in the cell file)"); }, nullptr);
    } else {
        add.group("Communications");
        auto linkText = [d](const char* key, const std::string& none) {
            return [d, key, none] { const std::string v = std::as_const(d().link)[key].str(); return v.empty() ? none : v; };
        };
        // OpenPnP's Communications Type: a serial port, or TCP.
        add.choice("communicationsType", "Communications Type", { "serial", "tcp" }, linkText("type", "serial"),
                   [d](const std::string& v) { d().link["type"] = v; });
        f.reshaping.push_back("communicationsType");
        add.choice("lineEnding", "Line-Endings", { "LF", "CR", "CRLF" }, linkText("lineEnding", "LF"),
                   [d](const std::string& v) { d().link["lineEnding"] = v; });
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
    add.integer("identifyTimeoutMs", "Identify Timeout [ms]", [d]() -> int& { return d().identifyTimeoutMs; }, 100, 60000);
    add.integer("homeTimeoutMs", "Home Timeout [ms]", [d]() -> int& { return d().homeTimeoutMs; }, 1000, 600000);
    add.integer("statusIntervalMs", "Status Interval [ms]", [d]() -> int& { return d().statusIntervalMs; }, 10, 10000);
    add.note("Max. Feed Rate 0: moves are as fast as their axes allow.");

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
    add.tab("Configuration");
    add.group("Properties");
    // A controller axis is one a controller drives; a mapped one follows
    // another through a straight-line map (a second Z driven the other way);
    // a virtual one is only a number jplacer keeps.
    // A cam one is OpenPnP's cam axis: a Z a cam turned by a rotation axis drives.
    const Strings kinds{ "controller", "mapped", "cam", "virtual" };
    add.choice("kind", "Kind", kinds, [a] { return std::string(A::kindName(a().kind)); },
               [a](const std::string& v) {
                   for (A::Kind k : { A::Kind::Controller, A::Kind::Mapped, A::Kind::Cam, A::Kind::Virtual })
                       if (v == A::kindName(k)) a().kind = k;
               });
    f.reshaping.push_back("kind");
    add.choice("type", "Type", { "x", "y", "z", "rotation" }, [a] { return std::string(A::typeName(a().type)); },
               [a](const std::string& v) {
                   for (A::Type t : { A::Type::X, A::Type::Y, A::Type::Z, A::Type::Rotation })
                       if (v == A::typeName(t)) a().type = t;
               });
    add.text("name", "Name", [a]() -> std::string& { return a().name; }, "name");
    if (a().kind == A::Kind::Controller) {
        add.group("Controller Settings");
        add.byName("driver", "Driver", named(cell.drivers, "(none)"), [a]() -> std::string& { return a().driverId; });
        add.text("letter", "Axis Letter", [a]() -> std::string& { return a().letter; });
        add.text("preMoveCommand", "Pre-Move Command", [a]() -> std::string& { return a().preMoveCommand; }, "long");
        add.tip("Sent before a move of this axis when its controller allows pre-move commands (and Letter Variables is "
                "off), {Coordinate} where the axis was: to switch an output shared by several axes to this one.");
        add.number("homeCoordinate", "Home Coordinate", [a]() -> double& { return a().homeCoordinate; });
        // One motor step, and its other side: steps per unit.
        const std::string unit = a().type == A::Type::Rotation ? "Degree" : "Millimeter";
        add.row(a().type == A::Type::Rotation ? "Resolution [Degrees]" : "Resolution [Millimeters]");
        add.number("resolution", "Resolution", [a]() -> double& { return a().resolution; }, 6);
        add.number("stepsPerUnit", "Steps / " + unit, [a] { return a().resolution > 0 ? 1 / a().resolution : 0.0; },
                   [a](double v) { a().resolution = v > 0 ? 1 / v : 0; }, 6);
        add.end();
        if (a().type == A::Type::Rotation) {
            add.flag("limitRotation", "Limit to Range", [a]() -> bool& { return a().limitRotation; });
            add.flag("wrapAroundRotation", "Wrap Around", [a]() -> bool& { return a().wrapAroundRotation; });
            add.note("Limit to Range keeps the angle within -180..180; Wrap Around turns the short way round.");
        }
    }
    if (a().kind == A::Kind::Virtual) {
        add.group("Virtual Axis");
        add.number("homeCoordinate", "Home / Safe Z", [a]() -> double& { return a().homeCoordinate; });
    }
    if (a().kind == A::Kind::Mapped) {
        add.group("Axis Mapping");
        add.byName("inputAxis", "Input Axis", named(cell.axes, "(none)"), [a]() -> std::string& { return a().inputAxisId; });
        add.header({ "Input", "Output" });
        add.row("Map Point A");
        add.number("mapInput0", "Point A input", [a]() -> double& { return a().mapInput0; });
        add.number("mapOutput0", "Point A output", [a]() -> double& { return a().mapOutput0; });
        add.end();
        add.row("Map Point B");
        add.number("mapInput1", "Point B input", [a]() -> double& { return a().mapInput1; });
        add.number("mapOutput1", "Point B output", [a]() -> double& { return a().mapOutput1; });
        add.end();
        add.number("homeCoordinate", "Home Coordinate", [a]() -> double& { return a().homeCoordinate; });
    }
    if (a().kind == A::Kind::Cam) {
        // OpenPnP's ReferenceCamCounterClockwiseAxis (and its clockwise partner, here the same axis turned the other way).
        add.group("Cam Settings");
        add.byName("inputAxis", "Input Axis", named(cell.axes, "(none)"), [a]() -> std::string& { return a().inputAxisId; });
        add.flag("camClockwise", "Clockwise?", [a]() -> bool& { return a().camClockwise; });
        add.tip("The cam's other side: the nozzle that goes down as the cam turns clockwise (OpenPnP's ReferenceCamClockwiseAxis).");
        add.number("camRadius", "Cam Radius", [a]() -> double& { return a().camRadius; });
        add.number("camArmsAngle", "Cam Arms Angle", [a]() -> double& { return a().camArmsAngle; });
        add.tip("The angle between the cam's two arms (180 for a straight cam); the balance point is at 0 with them folded out.");
        add.number("camWheelRadius", "Cam Wheel Radius", [a]() -> double& { return a().camWheelRadius; });
        add.number("camWheelGap", "Cam Wheel Gap", [a]() -> double& { return a().camWheelGap; });
        add.number("homeCoordinate", "Home Coordinate", [a]() -> double& { return a().homeCoordinate; });
        add.note("Z = Cam Radius x sin(angle + 90 - Cam Arms Angle / 2) + Cam Wheel Radius + Cam Wheel Gap, the angle that "
                 "of the input axis (the other way, clockwise), kept within the cam's useful range.");
    }
    add.group("Kinematic Settings");
    // Each limit with its switch, and buttons to take it from where the axis is or go there.
    auto limit = [&add, a, id](const std::string& key, const std::string& label, double A::*value, bool A::*on) {
        add.row(label, Place::Axis, id);
        add.number(key, label, [a, value]() -> double& { return a().*value; });
        add.flag(key + "Enabled", "Enabled?", [a, on]() -> bool& { return a().*on; });
        add.end();
    };
    limit("softLimitLow", "Soft Limit Low", &A::softLimitLow, &A::softLimitLowEnabled);
    limit("safeZoneLow", "Safe Zone Low", &A::safeZoneLow, &A::safeZoneLowEnabled);
    limit("safeZoneHigh", "Safe Zone High", &A::safeZoneHigh, &A::safeZoneHighEnabled);
    limit("softLimitHigh", "Soft Limit High", &A::softLimitHigh, &A::softLimitHighEnabled);
    add.row("Feed Rate [/s]");
    add.number("feedratePerSecond", "Feed Rate [/s]", [a]() -> double& { return a().feedratePerSecond; }, 1);
    add.number("feedratePerMinute", "Feed Rate [/min]", [a] { return a().feedratePerSecond * 60; },
               [a](double v) { a().feedratePerSecond = v / 60; }, 1);
    add.end();
    add.number("accelerationPerSecond2", "Acceleration [/s\u00B2]", [a]() -> double& { return a().accelerationPerSecond2; }, 1);
    add.number("jerkPerSecond3", "Jerk [/s\u00B3]", [a]() -> double& { return a().jerkPerSecond3; }, 1);
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
        add.number("approachMm", "Least Approach", [a] { return a().approachMm; },
                   [a](double v) { if (v >= 0) a().approachMm = v; });
        add.number("backlashSpeedFactor", "Speed Factor", [a]() -> double& { return a().backlashSpeedFactor; }, 2);
    } else if (method != A::Backlash::None) {
        add.number("backlashOffset", "Backlash Offset", [a]() -> double& { return a().backlashOffset; });
        if (method == A::Backlash::DirectionalSneakUp)
            add.number("sneakUp", "Sneak-up Distance", [a] { return a().sneakUpMm; },
                       [a](double v) { if (v >= 0) a().sneakUpMm = v; });
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
            add.number(key + "X", what + " X", [at]() -> double& { return at()->x; });
            add.number(key + "Y", what + " Y", [at]() -> double& { return at()->y; });
            if (withZ) add.number(key + "Z", what + " Z", [at]() -> double& { return at()->z; });
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
        add.number("homingFiducialDiameter", "Fiducial Diameter", [h]() -> double& { return h().homingFiducialDiameter; });
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
    add.number("rigPrimaryDiameter", "Primary Diameter", [h]() -> double& { return h().rigPrimaryDiameter; });
    add.number("rigSecondaryDiameter", "Secondary Diameter", [h]() -> double& { return h().rigSecondaryDiameter; });
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
    add.choice("rotationMode", "Rotation Mode", { "AbsolutePartAngle", "PlacementAngle", "MinimalRotation", "LimitedArticulation" },
               [n] { return n().rotationMode; }, [n](const std::string& v) { n().rotationMode = v; });
    f.reshaping.push_back("rotationMode");
    if (n().rotationMode == "LimitedArticulation") {
        add.number("maxPickArticulation", "Max. Pick Articulation", [n]() -> double& { return n().maxPickArticulation; }, 1);
        add.number("maxAlignArticulation", "Max. Alignment Articulation", [n]() -> double& { return n().maxAlignArticulation; }, 1);
    }
    add.note("How the nozzle turns for a part, as OpenPnP's: AbsolutePartAngle, to the part's own angle at pick and at "
             "place; PlacementAngle, picked already turned against its placement so it places at 0; MinimalRotation, "
             "picked at whatever angle the nozzle has; LimitedArticulation, for a nozzle with a limited turn (its "
             "rotation axis limited to range, within its soft limits): about the middle of the range, room left "
             "for the pick and alignment corrections.");

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
        add.number("manualX", "Manual Change X", [manual]() -> double& { return manual()->x; });
        add.number("manualY", "Manual Change Y", [manual]() -> double& { return manual()->y; });
        add.number("manualZ", "Manual Change Z", [manual]() -> double& { return manual()->z; });
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
}

void nozzleTipForm(JPCellConfig& cell, const std::string& id, JPSetupProperties::Form& f) {
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
    add.number("diameterLowMm", "Outside Diameter", [t]() -> double& { return t().diameterLowMm; });
    add.tip("Outside diameter of the nozzle tip at the lowest ~0.75mm.");
    add.group("Part Dimensions");
    add.number("diameter", "Diameter Seen From Below", [t]() -> double& { return t().diameter; });
    add.number("minPartDiameterMm", "Min. Part Diameter", [t]() -> double& { return t().minPartDiameterMm; });
    add.tip("Minimum part diameter, to be picked with this the nozzle tip: at least the tip's air bore plus two times the "
            "Max. Pick Tolerance.");
    add.number("maxPartDiameterMm", "Max. Part Diameter", [t]() -> double& { return t().maxPartDiameterMm; });
    add.tip("Maximum diameter/diagonal of parts picked with this nozzle tip, including tolerances.");

    add.number("maxPartHeightMm", "Max. Part Height", [t]() -> double& { return t().maxPartHeightMm; });
    add.tip("Maximum part heights picked with this nozzle tip. Used for dynamic safe Z, if part height is unknown.");
    add.number("maxPickToleranceMm", "Max. Pick Tolerance", [t]() -> double& { return t().maxPickToleranceMm; });
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
        add.choice(key + "Method", "Measurement Method", { "None", "Absolute", "Difference" },
                   [sensing] { return sensing().method; }, [sensing](const std::string& v) { sensing().method = v; });
        f.reshaping.push_back(key + "Method");
        if (sensing().method == "None") continue;
        if (!on) {
            add.row("Probing Time (ms)");
            add.integer("partOffProbingMs", "Probing Time (ms)", [t]() -> int& { return t().partOffProbingMs; }, 0, 60000);
            add.integer("partOffDwellMs", "Dwell Time (ms)", [t]() -> int& { return t().partOffDwellMs; }, 0, 60000);
            add.end();
        }
        add.header({ "Low", "High" });
        add.row(sensing().method == "Difference" ? "Vacuum Level Before" : "Vacuum Level");
        add.number(key + "Low", "Vacuum Low", [sensing]() -> double& { return sensing().low; }, 1);
        add.number(key + "High", "Vacuum High", [sensing]() -> double& { return sensing().high; }, 1);
        add.end();
        if (sensing().method == "Difference") {
            add.row("Vacuum Difference");
            add.number(key + "DiffLow", "Difference Low", [sensing]() -> double& { return sensing().diffLow; }, 1);
            add.number(key + "DiffHigh", "Difference High", [sensing]() -> double& { return sensing().diffHigh; }, 1);
            add.end();
        }
    }
    add.note("Read from the nozzle's sensing actuator (else its vacuum actuator) after a pick and after a place "
             "(the place's probe: the valve open for the probing time, then closed for the dwell). Absolute: the "
             "level within Low..High. Difference: the level just before within Low..High, and its change within "
             "the difference's.");

    add.tab("Tool Changer");
    add.group("Nozzle Tip Changer");
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

    add.tab("Calibration");
    add.group("Runout");
    auto rc = [t]() -> JPNozzleTipConfig::RunoutCalibration& { return t().runoutCalibration; };
    using RC = JPNozzleTipConfig::RunoutCalibration;
    add.flag("runoutEnabled", "Compensate?", [rc]() -> bool& { return rc().enabled; });
    add.integer("runoutDivisions", "Circle Divisions", [rc]() -> int& { return rc().divisions; }, RC::kLeastDivisions,
                RC::kMostDivisions);
    add.integer("runoutMisdetects", "Allowed Misdetects", [rc]() -> int& { return rc().misdetects; }, 0, RC::kMostDivisions);
    add.number("runoutZOffset", "Calibration Z Offset", [rc]() -> double& { return rc().zOffset; });
    add.number("runoutVisionDiameter", "Vision Diameter", [rc] { return rc().visionDiameter; },
               [rc](double v) { if (v >= 0) rc().visionDiameter = v; });
    add.actions({ { "Calibrate", "calibrateRunout" }, { "Reset", "resetRunout" } });
    add.note("Calibrate measures the tip on the nozzle it is on, over the fixed camera looking up: down to the "
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
void calibrationResults(JPFormBuilder& add, const JPCameraCalibration& cal, bool looksUp) {
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
    std::snprintf(b, sizeof b, "%.2f \xC3\x97 %.2f \xC2\xB5m", umX, umY);
    shown("upp", "Units Per Pixel", b);
    std::snprintf(b, sizeof b, "%.3f \xC3\x97 %.3f px/mm", cal.scaleX(), cal.scaleY());
    shown("scale", "Scale", b);
    std::snprintf(b, sizeof b, "%.2f \xC2\xB5m (%.3f px)", cal.rmsPx * (umX + umY) / 2, cal.rmsPx);
    shown("accuracy", "Estimated Locating Accuracy", b);
    std::snprintf(b, sizeof b, "%.2f \xC3\x97 %.2f mm", cal.width / cal.scaleX(), cal.height / cal.scaleY());
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

void cameraForm(JPCellConfig& cell, const std::string& id, JPSetupProperties::Form& f) {
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
    auto device = [c]() -> JJson& { return c().device; };
    add.group("Light");
    add.byName("light", "Light Actuator", named(cell.actuators, "(none)"), [device] { return std::as_const(device())["light-actuator-id"].str(); },
               [device](const std::string& v) { device()["light-actuator-id"] = v; });
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
    add.number("unitsPerPixelX", "Units per Pixel X", [c]() -> double& { return c().unitsPerPixelX; }, 5);
    add.number("unitsPerPixelY", "Units per Pixel Y", [c]() -> double& { return c().unitsPerPixelY; }, 5);
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
    add.choice("settleMethod", "Settle Method", { "FixedTime", "Maximum", "Mean", "Euclidean", "Square" },
               [settle] { return settle().method; }, [settle](const std::string& v) { settle().method = v; });
    f.reshaping.push_back("settleMethod");
    if (settle().method == "FixedTime") {
        add.integer("settleTimeMs", "Settle Time (ms)", [settle]() -> int& { return settle().timeMs; }, 0, 10000);
        add.note("A picture for vision is one taken this long after the move ended.");
    } else {
        add.row("Settle Threshold");
        add.number("settleThreshold", "Settle Threshold", [settle]() -> double& { return settle().threshold; }, 3);
        add.integer("settleTimeoutMs", "Settle Timeout (ms)", [settle]() -> int& { return settle().timeoutMs; }, 0, 60000);
        add.end();
        add.row("Debounce Frames");
        add.integer("settleDebounce", "Debounce Frames", [settle]() -> int& { return settle().debounce; }, 0, 100);
        add.number("settleMaskCircle", "Center Mask", [settle]() -> double& { return settle().maskCircle; }, 3);
        add.end();
        add.note("Each picture is compared with the one before (as a percentage of full scale), in a centred "
                 "circle of Center Mask of the picture (0: all of it), until the difference stays under the "
                 "threshold for Debounce Frames more pictures, or the timeout passes.");
    }
    add.group("Test");
    add.actions({ { "Left", "settleTestLeft" }, { "Right", "settleTestRight" }, { "Back", "settleTestBack" },
                  { "Front", "settleTestFront" }, { "Here", "settleTestHere" } });
    add.note(c().mount.headId.empty()
                 ? "Move the nozzle chosen on the Jog pad one jog step (the Jog pad's distance) that way and back, "
                   "or not at all (Here), let the camera settle as a picture for vision would, and graph how it came "
                   "to rest. Put the nozzle over the camera, at its focus, first: only X and Y move."
                 : "Move the camera one jog step (the Jog pad's distance) that way and back, or not at all (Here), "
                   "let it settle as a picture for vision would, and graph how it came to rest.");
    if (const auto& t = c().settleTrace) {
        auto g = std::make_shared<JPPlot>();
        g->kind = JPPlot::Kind::Lines;
        g->xTitle = "ms";
        g->yTitle = "difference %";
        JPPlot::Series d{ "difference", JPPlot::Tone::First, {} };
        for (const auto& [ms, v] : t->points) d.points.push_back({ ms, v });
        g->series.push_back(d);
        if (t->threshold > 0 && !t->points.empty())
            g->series.push_back({ "threshold", JPPlot::Tone::Second,
                                  { { t->points.front().first, t->threshold }, { t->points.back().first, t->threshold } } });
        if (t->settledMs >= 0) {
            double top = t->threshold;
            for (const auto& [ms, v] : t->points) top = std::max(top, v);
            g->series.push_back({ "settled", JPPlot::Tone::Muted, { { t->settledMs, 0 }, { t->settledMs, top } } });
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
    }

    add.tab("Device Settings");
    add.group("Device");
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
        add.text("source", "Source File", [device] { return std::as_const(device())["source"].str(); },
                 [device](const std::string& v) { device()["source"] = v; }, "long");
        add.tip("A PNG of the machine's table, as the camera would see it from straight above.");
        add.header({ "X", "Y" });
        add.row("Image Units Per Pixel");
        add.number("imageUppX", "Units Per Pixel X", num("imageUnitsPerPixel", "x", 0.04).first, num("imageUnitsPerPixel", "x", 0.04).second, 5);
        add.number("imageUppY", "Units Per Pixel Y", num("imageUnitsPerPixel", "y", 0.04).first, num("imageUnitsPerPixel", "y", 0.04).second, 5);
        add.end();
        add.row("Image Offset");
        add.number("imageOffsetX", "Offset X", num("imageOffset", "x", 0).first, num("imageOffset", "x", 0).second);
        add.number("imageOffsetY", "Offset Y", num("imageOffset", "y", 0).first, num("imageOffset", "y", 0).second);
        add.end();
        add.endColumns();
        add.number("simulatedRotation", "Simulated Rotation", num("simulatedRotation", nullptr, 0).first, num("simulatedRotation", nullptr, 0).second);
        add.number("simulatedScale", "Simulated Scale", num("simulatedScale", nullptr, 1).first, num("simulatedScale", nullptr, 1).second);
        add.flag("simulatedFlipped", "Simulated Flipped?", [device] { return std::as_const(device())["simulatedFlipped"].boolean(); },
                 [device](bool v) { device()["simulatedFlipped"] = v; });
    } else if (std::as_const(device())["backend"].str() == "mjpg") {
        // OpenPnP's MjpgCaptureCameraWizard.
        add.text("url", "MJPG URL", [device] { return std::as_const(device())["url"].str(); },
                 [device](const std::string& v) { device()["url"] = v; }, "long");
        add.tip("The camera's stream: http://host:port/path.");
        add.integer("timeoutMs", "Timeout [ms]", [device] { return int(std::as_const(device())["timeoutMs"].number(3000)); },
                    [device](int v) { device()["timeoutMs"] = v; }, 100, 60000);
    } else if (std::as_const(device())["backend"].str() == "simulated") {
        add.text("backend", "Device", [] { return std::string("simulated (set up in the cell file)"); }, nullptr);
    } else {
        // Found by the name the device gives itself, whichever socket it is in.
        add.text("device", "Device", [device] { return std::as_const(device())["name"].str(); },
                 [device](const std::string& v) { device()["name"] = v; }, "long");
    }
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

    // The camera's own settings: each one jplacer sets when it opens the
    // camera (by hand, or automatic where the camera can), or leaves as the
    // camera has it.
    add.group("Properties");
    add.header({ "Set?", "Auto", "Value" });
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
        if (set)
            add.integer(name + ":value", k.label, [control] { return int(std::as_const(control())["value"].number()); },
                        [control](int v) { control()["value"] = v; }, -1000000, 1000000);
        else
            add.skip();
        add.end();
    }
    add.note("Set? unticked: the camera keeps its own setting. The values are the camera's own units.");

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
            add.number(std::string(gamma ? "gamma" : "balance") + channel[ch], std::string(channel[ch]) + " " + row,
                       [wb, ch, gamma]() -> double& { return gamma ? wb().gamma[ch] : wb().balance[ch]; }, 3);
        add.end();
    }
    add.actions({ { "Overall", "whiteBalanceOverall" }, { "Brightest", "whiteBalanceBrightest" }, { "Reset", "whiteBalanceReset" } });
    add.note("Each channel is scaled by its balance, then given its gamma. Overall and Brightest work them out "
             "from what the camera sees now (something white or grey in view): the other channels brought up to "
             "the strongest, measured over the brighter fifth of the picture, or at its edge.");

    add.tab("Position");
    coordinateSystem<JPCameraConfig>(add, cell, c, "(fixed to the machine)", true, f);

    add.tab("Advanced Calibration");
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
    add.row("Lead-in (mm)");
    add.number("calLeadIn", "Lead-in (mm)", [k] { return k().leadInMm; }, [k](double v) { if (v >= 0) k().leadInMm = v; }, 2);
    add.integer("calFrames", "Pictures Each", [k]() -> int& { return k().frames; }, 1, JPCameraConfig::Calibrating::kMostFrames);
    add.end();
    add.flag("calTwoHeights", "Two Heights?", [k]() -> bool& { return k().twoHeights; });
    if (c().mount.headId.empty())
        add.number("calRaise", "Raise For The Second (mm)", [k] { return k().raiseMm; },
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
    for (const JPCameraCalibration& cal : c().calibrations) calibrationResults(add, cal, c().looksUp);
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
    add.group("Coordinate System");
    add.byName("head", "Head", named(cell.heads, "(on the machine)"), [a]() -> std::string& { return a().mount.headId; });
    f.reshaping.push_back("head");
    add.flag("axisInterlock", "Axis Interlock?", [a]() -> bool& { return a().interlock.enabled; });
    add.tip("Enable to get an extra Wizard tab to configure an Axis Interlocking Actuator");
    f.reshaping.push_back("axisInterlock");
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
    add.number("maxLinearOffsetMm", "Max. linear offset", [&v]() -> double& { return v.maxLinearOffsetMm; });
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
    add.number("fiducialMaxDistanceMm", "Max. Distance (old pipelines only)", [&v]() -> double& { return v.fiducialMaxDistanceMm; });
    add.tip("Maximum allowed distance between nominal fiducial location and detected location. This only applies where the "
            "vision pipeline does not have a maxDistance stage.");
    finder(add, v.fiducialPipeline, "fiducials");
    defaultSettingsTab(add, config, v.fiducialVisionId, false, tests);
}

} // namespace

JPSetupProperties::Form JPSetupProperties::forNode(JPCellConfig& cell, const std::string& path, const std::vector<JPFirmwareProfile>& profiles,
                                                   JPConfiguration* config, const JPVisionTests* tests) {
    Form f;
    const JPSetupTree::Path p = JPSetupTree::parse(path);
    if (p.kind == "machine") machineForm(cell, f);
    else if (p.kind == "jobprocessor") jobProcessorForm(cell, f);
    else if (p.kind == "vision" && p.id == "bottom") bottomVisionForm(cell, f, config, tests);
    else if (p.kind == "vision" && p.id == "fiducial") fiducialLocatorForm(cell, f, config, tests);
    else if (p.kind == "driver" && has(cell.drivers, p.id)) driverForm(cell, p.id, profiles, f);
    else if (p.kind == "axis" && has(cell.axes, p.id)) axisForm(cell, p.id, f);
    else if (p.kind == "head" && has(cell.heads, p.id)) headForm(cell, p.id, f);
    else if (p.kind == "nozzle" && has(cell.nozzles, p.id)) nozzleForm(cell, p.id, f);
    else if (p.kind == "nozzletip" && has(cell.nozzleTips, p.id)) nozzleTipForm(cell, p.id, f);
    else if (p.kind == "step" && has(cell.nozzleTips, p.owner)) stepForm(cell, p, f);
    else if (p.kind == "camera" && has(cell.cameras, p.id)) cameraForm(cell, p.id, f);
    else if (p.kind == "actuator" && has(cell.actuators, p.id)) actuatorForm(cell, p.id, f);
    else if (p.kind == "signaler" && has(cell.signalers, p.id)) signalerForm(cell, p.id, f);
    return f;
}

} // inline namespace jf
