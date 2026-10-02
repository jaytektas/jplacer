// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPOpenPnpMachineImporter.h"

#include "JPXmlReader.h"

#include "common/JPlacerLog.h"

#include <j/core/Log.h>

#include <cstdlib>
#include <map>
#include <regex>

inline namespace jf {

namespace {

// OpenPnP's class names are Java ones; the last segment says what it is.
std::string shortClass(const JPXmlElement& e) {
    const std::string& c = e.attr("class");
    const size_t dot = c.rfind('.');
    return dot == std::string::npos ? c : c.substr(dot + 1);
}

// A length element (<x value="..." units="Millimeters"/>) or attribute, in mm.
double toMm(double value, const std::string& units) {
    if (units == "Centimeters") return value * 10;
    if (units == "Meters")      return value * 1000;
    if (units == "Inches")      return value * 25.4;
    if (units == "Mils")        return value * 0.0254;
    if (units == "Microns")     return value / 1000;
    return value;
}

double lengthChild(const JPXmlElement& e, const char* child) {
    const JPXmlElement* c = e.child(child);
    if (!c) return 0;
    return toMm(std::strtod(c->attr("value").c_str(), nullptr), c->attr("units"));
}

double number(const std::string& s) { return std::strtod(s.c_str(), nullptr); }
bool   yes(const std::string& s)    { return s == "true"; }

// Strip OpenPnP's "; comment" tails and surrounding blanks.
std::string clean(std::string s) {
    if (const size_t semi = s.find(';'); semi != std::string::npos) s.erase(semi);
    const size_t a = s.find_first_not_of(" \t\r\n");
    const size_t b = s.find_last_not_of(" \t\r\n");
    return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
}

// OpenPnP's command templates ("{True:M64}{False:M65} P{Index}") in jplacer's
// form. `on` picks the True or False branch for a switch; Index becomes
// {index}; Value becomes {value}. Anything else is kept and noted.
std::string translate(const std::string& tmpl, int on, const std::string& what,
                      std::vector<std::string>& notes) {
    std::string out;
    for (size_t i = 0; i < tmpl.size(); ++i) {
        if (tmpl[i] != '{') { out += tmpl[i]; continue; }
        const size_t close = tmpl.find('}', i);
        if (close == std::string::npos) { out += tmpl.substr(i); break; }
        const std::string token = tmpl.substr(i + 1, close - i - 1);
        const size_t colon = token.find(':');
        const std::string name = token.substr(0, colon);
        const std::string fmt  = colon == std::string::npos ? std::string() : token.substr(colon + 1);
        if (name == "True" || name == "False") {
            if ((name == "True") == (on == 1)) out += fmt;
        } else if (name == "Index") {
            out += "{index}";
        } else if (name == "Value" || name == "DoubleValue" || name == "IntegerValue") {
            out += "{value}";
        } else {
            out += "{" + token + "}";
            notes.push_back(what + ": the template variable {" + name + "} has no jplacer equivalent yet; it is kept as written");
        }
        i = close;
    }
    return clean(out);
}

// OpenPnP regexes use Java's named groups; std::regex does not have them.
std::string plainGroups(const std::string& re) {
    return std::regex_replace(re, std::regex(R"(\(\?<[A-Za-z][A-Za-z0-9]*>)"), "(");
}

std::string serialPort(const std::string& name) {
    if (name.empty() || name.find('/') != std::string::npos || name.rfind("COM", 0) == 0) return name;
    return "/dev/" + name;
}

JPMountConfig mount(const JPXmlElement& e, const std::string& headId) {
    JPMountConfig m;
    m.headId       = headId;
    m.axisX        = e.attr("axis-X-id");
    m.axisY        = e.attr("axis-Y-id");
    m.axisZ        = e.attr("axis-Z-id");
    m.axisRotation = e.attr("axis-rotation-id");
    if (const JPXmlElement* off = e.child("head-offsets")) {
        m.offsetX = toMm(number(off->attr("x")), off->attr("units"));
        m.offsetY = toMm(number(off->attr("y")), off->attr("units"));
        m.offsetZ = toMm(number(off->attr("z")), off->attr("units"));
    }
    return m;
}

// The driver's command templates: (type, head-mountable id) -> text. An
// entry with no id is the driver's default for that type.
using Commands = std::map<std::pair<std::string, std::string>, std::string>;

const std::string* findCommand(const Commands& cmds, const std::string& type, const std::string& id) {
    if (const auto it = cmds.find({ type, id }); it != cmds.end()) return &it->second;
    if (const auto it = cmds.find({ type, "" });  it != cmds.end()) return &it->second;
    return nullptr;
}

} // namespace

bool JPOpenPnpMachineImporter::import(const std::string& machineXml, JPCellConfig& cell,
                                      std::vector<std::string>& notes, std::string& error) {
    JPXmlElement doc;
    if (!JPXmlReader::read(machineXml, doc, error)) {
        JLOGC(JPlacerLog::kImport, JLogLevel::Error) << error;
        return false;
    }
    const JPXmlElement* machine = doc.child("machine");
    if (doc.name != "openpnp-machine" || !machine) {
        error = machineXml + ": not an OpenPnP machine.xml";
        return false;
    }

    JPCellConfig c;
    c.name = "Imported from OpenPnP";
    std::map<std::string, Commands> commands;   // by driver id

    if (const JPXmlElement* drivers = machine->child("drivers")) {
        for (const JPXmlElement& d : drivers->children) {
            const std::string kind = shortClass(d);
            if (kind != "GcodeDriver" && kind != "GcodeAsyncDriver") {
                notes.push_back("controller " + d.attr("name") + " (" + kind + ") is not a G-code controller and was left out");
                continue;
            }
            JPDriverConfig dc;
            dc.id   = d.attr("id");
            dc.name = d.attr("name");
            dc.link = JJson::object();
            if (d.attr("communications") == "tcp") {
                const JPXmlElement* tcp = d.child("tcp");
                dc.link["type"] = "tcp";
                dc.link["host"] = tcp ? tcp->attr("ip-address") : std::string();
                dc.link["port"] = tcp ? number(tcp->attr("port")) : 0.0;
                notes.push_back("controller " + dc.name + " is reached over TCP, which jplacer does not connect to yet");
            } else {
                const JPXmlElement* serial = d.child("serial");
                dc.link["type"] = "serial";
                dc.link["port"] = serial ? serialPort(serial->attr("port-name")) : std::string();
                dc.link["baud"] = serial ? number(serial->attr("baud")) : 0.0;
                const std::string flow = serial ? serial->attr("flow-control") : std::string();
                dc.link["flowControl"] = flow == "RtsCts" ? "rtscts" : flow == "XonXoff" ? "xonxoff" : "none";
            }
            if (const double t = number(d.attr("timeout-milliseconds")); t > 0) dc.commandTimeoutMs = int(t);
            if (const double t = number(d.attr("infinity-timeout-milliseconds")); t > 0) dc.homeTimeoutMs = int(t);

            Commands& cmds = commands[dc.id];
            for (const JPXmlElement& cmd : d.children)
                if (cmd.name == "command")
                    if (const JPXmlElement* text = cmd.child("text"))
                        cmds[{ cmd.attr("type"), cmd.attr("head-mountable-id") }] = text->text;
            // How this machine homes is its own: OpenPnP's home command is
            // this controller's, over its firmware profile's.
            if (const std::string* home = findCommand(cmds, "HOME_COMMAND", ""))
                if (const std::string t = translate(*home, -1, "controller " + dc.name, notes); !t.empty())
                    dc.commands["home"] = t;
            c.drivers.push_back(std::move(dc));
        }
    }

    if (const JPXmlElement* axes = machine->child("axes")) {
        for (const JPXmlElement& x : axes->children) {
            const std::string kind = shortClass(x);
            JPAxisConfig a;
            a.id   = x.attr("id");
            a.name = x.attr("name");
            const std::string& type = x.attr("type");
            a.type = type == "Y" ? JPAxisConfig::Type::Y : type == "Z" ? JPAxisConfig::Type::Z
                   : type == "Rotation" ? JPAxisConfig::Type::Rotation : JPAxisConfig::Type::X;
            a.homeCoordinate = lengthChild(x, "home-coordinate");
            if (kind == "ReferenceControllerAxis") {
                a.kind     = JPAxisConfig::Kind::Controller;
                a.driverId = x.attr("driver-id");
                a.letter   = x.attr("letter");
                a.softLimitLow  = lengthChild(x, "soft-limit-low");
                a.softLimitHigh = lengthChild(x, "soft-limit-high");
                a.softLimitLowEnabled  = yes(x.attr("soft-limit-low-enabled"));
                a.softLimitHighEnabled = yes(x.attr("soft-limit-high-enabled"));
                a.safeZoneLow  = lengthChild(x, "safe-zone-low");
                a.safeZoneHigh = lengthChild(x, "safe-zone-high");
                a.safeZoneLowEnabled  = yes(x.attr("safe-zone-low-enabled"));
                a.safeZoneHighEnabled = yes(x.attr("safe-zone-high-enabled"));
                a.backlashOffset         = lengthChild(x, "backlash-offset");
                a.feedratePerSecond      = lengthChild(x, "feedrate-per-second");
                a.accelerationPerSecond2 = lengthChild(x, "acceleration-per-second-2");
                a.jerkPerSecond3         = lengthChild(x, "jerk-per-second-3");
                a.wrapAroundRotation     = yes(x.attr("wrap-around-rotation"));
                a.limitRotation          = yes(x.attr("limit-rotation"));
            } else if (kind == "ReferenceVirtualAxis") {
                a.kind = JPAxisConfig::Kind::Virtual;
            } else if (kind == "ReferenceMappedAxis") {
                a.kind        = JPAxisConfig::Kind::Mapped;
                a.inputAxisId = x.attr("input-axis-id");
                a.mapInput0   = lengthChild(x, "map-input-0");
                a.mapOutput0  = lengthChild(x, "map-output-0");
                a.mapInput1   = lengthChild(x, "map-input-1");
                a.mapOutput1  = lengthChild(x, "map-output-1");
            } else {
                notes.push_back("axis " + a.name + " (" + kind + ") has no jplacer equivalent yet and was left out");
                continue;
            }
            c.axes.push_back(std::move(a));
        }
    }

    // Actuators first: nozzles name theirs.
    std::map<std::string, std::string> actuatorIdByName;
    auto addActuator = [&](const JPXmlElement& x, const std::string& headId) {
        JPActuatorConfig a;
        a.id       = x.attr("id");
        a.name     = x.attr("name");
        a.driverId = x.attr("driver-id");
        a.mount    = mount(x, headId);
        a.index    = x.attr("index");
        const std::string& vt = x.attr("value-type");
        a.valueType = vt == "Double" ? JPActuatorConfig::ValueType::Number
                    : vt == "String" ? JPActuatorConfig::ValueType::Text : JPActuatorConfig::ValueType::Boolean;
        // No controller named: OpenPnP still files its commands under its id
        // on the controller that sends them.
        if (a.driverId.empty())
            for (const auto& [driverId, cmds] : commands)
                for (const auto& [key, text] : cmds)
                    if (key.second == a.id) a.driverId = driverId;
        if (shortClass(x) != "ReferenceActuator")
            notes.push_back("actuator " + a.name + " (" + shortClass(x) + ") was imported as a plain actuator");
        const auto cmds = commands.find(a.driverId);
        if (cmds != commands.end()) {
            if (const std::string* t = findCommand(cmds->second, "ACTUATE_BOOLEAN_COMMAND", a.id)) {
                a.onCommand  = translate(*t, 1, "actuator " + a.name, notes);
                a.offCommand = translate(*t, 0, "actuator " + a.name, notes);
            }
            if (const std::string* t = findCommand(cmds->second, "ACTUATOR_READ_COMMAND", a.id))
                a.readCommand = translate(*t, -1, "actuator " + a.name, notes);
            if (const std::string* t = findCommand(cmds->second, "ACTUATOR_READ_REGEX", a.id))
                a.readPattern = plainGroups(*t);
        }
        actuatorIdByName[a.name] = a.id;
        c.actuators.push_back(std::move(a));
    };
    auto addCamera = [&](const JPXmlElement& x, const std::string& headId) {
        JPCameraConfig cam;
        cam.id      = x.attr("id");
        cam.name    = x.attr("name");
        cam.looksUp = x.attr("looking") == "Up";
        cam.mount   = mount(x, headId);
        if (const JPXmlElement* upp = x.child("units-per-pixel")) {
            cam.unitsPerPixelX = toMm(number(upp->attr("x")), upp->attr("units"));
            cam.unitsPerPixelY = toMm(number(upp->attr("y")), upp->attr("units"));
        }
        cam.device = JJson::object();
        cam.device["openpnpClass"] = shortClass(x);
        for (const char* key : { "unique-id", "format-id", "fps", "rotation", "flip-x", "flip-y", "light-actuator-id" })
            if (!x.attr(key).empty()) cam.device[key] = x.attr(key);
        c.cameras.push_back(std::move(cam));
    };

    if (const JPXmlElement* heads = machine->child("heads")) {
        for (const JPXmlElement& h : heads->children) {
            c.heads.push_back({ h.attr("id"), h.attr("name") });
            if (const std::string& vh = h.attr("visual-homing-method"); !vh.empty() && vh != "None")
                notes.push_back("head " + h.attr("name") + ": OpenPnP finishes homing by finding a fiducial with the "
                                "camera, which jplacer does not do yet. After Home, the axes take their home "
                                "coordinates as they are, so the head must be at its home position first");
            if (const JPXmlElement* acts = h.child("actuators"))
                for (const JPXmlElement& x : acts->children) addActuator(x, h.attr("id"));
            if (const JPXmlElement* cams = h.child("cameras"))
                for (const JPXmlElement& x : cams->children) addCamera(x, h.attr("id"));
            if (const JPXmlElement* nozzles = h.child("nozzles")) {
                for (const JPXmlElement& x : nozzles->children) {
                    JPNozzleConfig n;
                    n.id    = x.attr("id");
                    n.name  = x.attr("name");
                    n.mount = mount(x, h.attr("id"));
                    if (const JPXmlElement* v = x.child("vacuum-actuator-name")) {
                        const auto it = actuatorIdByName.find(v->text);
                        if (it != actuatorIdByName.end()) n.vacuumActuatorId = it->second;
                    }
                    c.nozzles.push_back(std::move(n));
                }
            }
        }
    }
    if (const JPXmlElement* acts = machine->child("actuators"))
        for (const JPXmlElement& x : acts->children) addActuator(x, std::string());
    if (const JPXmlElement* cams = machine->child("cameras"))
        for (const JPXmlElement& x : cams->children) addCamera(x, std::string());

    for (const std::string& p : c.problems()) notes.push_back(p);
    JLOGC(JPlacerLog::kImport, JLogLevel::Info) << machineXml << ": " << c.drivers.size() << " controller(s), "
        << c.axes.size() << " axes, " << c.nozzles.size() << " nozzle(s), " << c.cameras.size() << " camera(s), "
        << c.actuators.size() << " actuator(s)";
    for (const std::string& n : notes) JLOGC(JPlacerLog::kImport, JLogLevel::Warn) << n;
    cell = std::move(c);
    return true;
}

} // inline namespace jf
