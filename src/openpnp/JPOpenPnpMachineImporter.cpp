// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPOpenPnpMachineImporter.h"

#include "JPXmlReader.h"

#include "common/JPlacerLog.h"

#include <j/core/Log.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <cstdlib>
#include <map>
#include <optional>
#include <regex>
#include <set>

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

// A location element (<park-location x=".." y=".." z=".." rotation=".." units=".."/>), in mm.
std::optional<JPMachineLocation> location(const JPXmlElement& parent, const char* child) {
    const JPXmlElement* e = parent.child(child);
    if (!e) return std::nullopt;
    const std::string& u = e->attr("units");
    return JPMachineLocation{ toMm(std::strtod(e->attr("x").c_str(), nullptr), u),
                       toMm(std::strtod(e->attr("y").c_str(), nullptr), u),
                       toMm(std::strtod(e->attr("z").c_str(), nullptr), u),
                       std::strtod(e->attr("rotation").c_str(), nullptr) };
}

bool   yes(const std::string& s)    { return s == "true"; }

// Strip OpenPnP's "; comment" tails and surrounding blanks.
std::string clean(std::string s) {
    if (const size_t semi = s.find(';'); semi != std::string::npos) s.erase(semi);
    const size_t a = s.find_first_not_of(" \t\r\n");
    const size_t b = s.find_last_not_of(" \t\r\n");
    return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
}

// One line of an OpenPnP command template ("{True:M64}{False:M65} P{Index}")
// in jplacer's form. `on` picks the True or False branch for a switch; Index
// becomes {index}; Value becomes {value}. Anything else is kept and noted.
std::string translateLine(const std::string& tmpl, int on, const std::string& what,
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
        } else if (name == "Value" || name == "DoubleValue" || name == "IntegerValue" || name == "StringValue") {
            out += "{value}";
        } else {
            out += "{" + token + "}";
            notes.push_back(what + ": the template variable {" + name + "} has no jplacer equivalent yet; it is kept as written");
        }
        i = close;
    }
    return clean(out);
}

// A whole command, line by line: OpenPnP keeps a multi-line command (a
// homing sequence) as one <text> per line. Lines that were only a comment are
// dropped; the order is kept, because on a machine it is everything.
std::string translate(const std::string& tmpl, int on, const std::string& what,
                      std::vector<std::string>& notes) {
    std::string out;
    size_t start = 0;
    while (start <= tmpl.size()) {
        const size_t eol = tmpl.find('\n', start);
        const std::string line = translateLine(
            tmpl.substr(start, eol == std::string::npos ? std::string::npos : eol - start), on, what, notes);
        start = eol == std::string::npos ? tmpl.size() + 1 : eol + 1;
        if (line.empty()) continue;
        out += (out.empty() ? "" : "\n") + line;
    }
    return out;
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
    // The machine's own settings.
    auto setting = [machine](const char* name) {
        const JPXmlElement* e = machine->child(name);
        return e && e->text.find("true") != std::string::npos;
    };
    const bool homeAfterEnabled = setting("home-after-enabled");   // every controller's
    c.parkAfterHome = setting("park-after-homed");
    c.discardLocation = location(*machine, "discard-location");
    // How a job is run, and the fiducial locator's tolerances.
    if (const JPXmlElement* jp = machine->child("pnp-job-processor")) {
        JPJobProcessorConfig& j = c.jobProcessor;
        auto number = [jp](const char* name, double def) {
            const std::string v = jp->attr(name);
            return v.empty() ? def : std::strtod(v.c_str(), nullptr);
        };
        auto flag = [jp](const char* name, bool def) {
            const std::string v = jp->attr(name);
            return v.empty() ? def : v == "true";
        };
        const auto& orders = JPJobProcessorConfig::jobOrderKeys();
        if (const auto it = std::find(orders.begin(), orders.end(), jp->attr("job-order")); it != orders.end())
            j.jobOrder = JPJobProcessorConfig::JobOrder(it - orders.begin());
        if (const JPXmlElement* planner = jp->child("planner")) {
            const auto& strategies = JPJobProcessorConfig::strategyKeys();
            if (const auto it = std::find(strategies.begin(), strategies.end(), planner->attr("strategy"));
                it != strategies.end())
                j.strategy = JPJobProcessorConfig::Strategy(it - strategies.begin());
        }
        j.maxVisionRetries = int(number("max-vision-retries", j.maxVisionRetries));
        j.maxPlacementRetries = int(number("max-placement-retries", j.maxPlacementRetries));
        j.feederFaultLimit = int(number("feeder-fault-limit", j.feederFaultLimit));
        j.feederFaultWindowSize = int(number("feeder-fault-window-size", j.feederFaultWindowSize));
        j.steppingToNextMotion = flag("stepping-to-next-motion", j.steppingToNextMotion);
        j.optimizeMultipleNozzles = flag("optimize-multiple-nozzles", j.optimizeMultipleNozzles);
        j.preRotateAllNozzles = flag("pre-rotate-all-nozzles", j.preRotateAllNozzles);
        j.fiducialLevel = int(number("fiducial-level", j.fiducialLevel));
    }
    // The machine's vision: its bottom vision (the first part alignment) and fiducial locator.
    if (const JPXmlElement* aligns = machine->child("part-alignments"))
        for (const JPXmlElement& a : aligns->children) {
            if (shortClass(a) != "ReferenceBottomVision") continue;
            JPVisionConfig& v = c.vision;
            v.bottomVisionEnabled = a.attr("enabled") != "false";
            if (!a.attr("bottom-vision-id").empty()) v.bottomVisionId = a.attr("bottom-vision-id");
            v.preRotate = a.attr("pre-rotate") != "false";
            if (!a.attr("max-vision-passes").empty()) v.maxVisionPasses = std::atoi(a.attr("max-vision-passes").c_str());
            if (!a.attr("max-angular-offset").empty()) v.maxAngularOffset = std::strtod(a.attr("max-angular-offset").c_str(), nullptr);
            if (!a.attr("test-alignment-angle").empty())
                v.testAlignmentAngle = std::strtod(a.attr("test-alignment-angle").c_str(), nullptr);
            if (a.child("max-linear-offset")) v.maxLinearOffsetMm = lengthChild(a, "max-linear-offset");
            break;
        }
    if (const JPXmlElement* fl = machine->child("fiducial-locator")) {
        if (!fl->attr("fiducial-vision-id").empty()) c.vision.fiducialVisionId = fl->attr("fiducial-vision-id");
        c.vision.enabledAveraging = fl->attr("enabled-averaging") == "true";
        if (fl->child("max-distance")) c.vision.fiducialMaxDistanceMm = lengthChild(*fl, "max-distance");
    }
    if (const JPXmlElement* fl = machine->child("fiducial-locator"))
        if (const JPXmlElement* t = fl->child("tolerances")) {
            JPJobProcessorConfig& j = c.jobProcessor;
            if (const JPXmlElement* e = t->child("scaling-tolerance")) j.scalingTolerance = std::strtod(e->text.c_str(), nullptr);
            if (const JPXmlElement* e = t->child("shearing-tolerance")) j.shearingTolerance = std::strtod(e->text.c_str(), nullptr);
            if (const JPXmlElement* e = t->child("board-location-tolerance")) {
                const double v = std::strtod(e->attr("value").c_str(), nullptr);
                j.boardLocationToleranceMm = e->attr("units") == "Inches" ? v * 25.4 : v;
            }
        }
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
                if (serial) {
                    // OpenPnP spells the settings out ("Eight", "One", "None").
                    const std::string bits = serial->attr("data-bits"), stop = serial->attr("stop-bits"), parity = serial->attr("parity");
                    dc.link["dataBits"] = bits == "Five" ? 5 : bits == "Six" ? 6 : bits == "Seven" ? 7 : 8;
                    dc.link["stopBits"] = stop == "Two" ? 2 : 1;
                    dc.link["parity"] = parity == "Even" ? "even" : parity == "Odd" ? "odd" : "none";
                    dc.link["setDtr"] = serial->attr("set-dtr") == "true";
                    dc.link["setRts"] = serial->attr("set-rts") == "true";
                    const std::string ending = serial->attr("line-ending-type");
                    dc.link["lineEnding"] = ending == "CR" ? "CR" : ending == "CRLF" ? "CRLF" : "LF";
                }
            }
            if (const double t = number(d.attr("timeout-milliseconds")); t > 0) dc.commandTimeoutMs = int(t);
            if (const double t = number(d.attr("infinity-timeout-milliseconds")); t > 0) dc.homeTimeoutMs = int(t);
            if (!d.attr("connect-wait-time-milliseconds").empty())
                dc.connectWaitMs = int(number(d.attr("connect-wait-time-milliseconds")));
            dc.maxFeedRate = number(d.attr("max-feed-rate"));
            dc.logGcode = d.attr("logging-gcode") == "true";

            Commands& cmds = commands[dc.id];
            for (const JPXmlElement& cmd : d.children) {
                if (cmd.name != "command") continue;
                std::string text;   // one <text> per line
                for (const JPXmlElement& t : cmd.children)
                    if (t.name == "text") text += (text.empty() ? "" : "\n") + t.text;
                cmds[{ cmd.attr("type"), cmd.attr("head-mountable-id") }] = text;
            }
            // How this machine homes is its own: OpenPnP's home command is
            // this controller's, over its firmware profile's.
            if (const std::string* home = findCommand(cmds, "HOME_COMMAND", ""))
                if (const std::string t = translate(*home, -1, "controller " + dc.name, notes); !t.empty())
                    dc.commands["home"] = t;
            dc.homeAfterConnect = homeAfterEnabled;
            c.drivers.push_back(std::move(dc));
        }
    }

    std::optional<std::string> squarenessAxis;   // OpenPnP's non-squareness transform axis, by id
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
                // OpenPnP's method as it is, with the values it calibrated for it.
                const std::string& method = x.attr("backlash-compensation-method");
                if (!method.empty() && method != "None" && a.backlashOffset != 0) {
                    a.backlash = method == "DirectionalCompensation"      ? JPAxisConfig::Backlash::Directional
                               : method == "DirectionalSneakUp"           ? JPAxisConfig::Backlash::DirectionalSneakUp
                               : method == "OneSidedOptimizedPositioning" ? JPAxisConfig::Backlash::OneSidedOptimized
                                                                          : JPAxisConfig::Backlash::OneSided;
                    a.backlashSpeedFactor = x.attr("backlash-speed-factor").empty() ? 1 : number(x.attr("backlash-speed-factor"));
                    a.sneakUpMm = lengthChild(x, "sneak-up-offset");
                }
                a.feedratePerSecond      = lengthChild(x, "feedrate-per-second");
                a.accelerationPerSecond2 = lengthChild(x, "acceleration-per-second-2");
                a.jerkPerSecond3         = lengthChild(x, "jerk-per-second-3");
                a.wrapAroundRotation     = yes(x.attr("wrap-around-rotation"));
                a.limitRotation          = yes(x.attr("limit-rotation"));
                if (const JPXmlElement* r = x.child("resolution")) a.resolution = number(r->text);
            } else if (kind == "ReferenceVirtualAxis") {
                a.kind = JPAxisConfig::Kind::Virtual;
            } else if (kind == "ReferenceMappedAxis") {
                a.kind        = JPAxisConfig::Kind::Mapped;
                a.inputAxisId = x.attr("input-axis-id");
                a.mapInput0   = lengthChild(x, "map-input-0");
                a.mapOutput0  = lengthChild(x, "map-output-0");
                a.mapInput1   = lengthChild(x, "map-input-1");
                a.mapOutput1  = lengthChild(x, "map-output-1");
            } else if (kind == "ReferenceLinearTransformAxis" && a.type == JPAxisConfig::Type::X
                       && std::abs(number(x.attr("factor-x")) - 1) < 1e-12 && number(x.attr("factor-z")) == 0
                       && number(x.attr("factor-rotation")) == 0 && !x.attr("input-axis-x-id").empty()
                       && !x.attr("input-axis-y-id").empty() && !squarenessAxis) {
                // OpenPnP's non-squareness: X = X + factorY Y + offset, which is
                // jplacer's squareness about Y = -offset / factorY. What rides on
                // this axis rides on its input X (see below).
                const double f = number(x.attr("factor-y")), offset = lengthChild(x, "offset");
                c.squareness.axisX = x.attr("input-axis-x-id");
                c.squareness.axisY = x.attr("input-axis-y-id");
                c.squareness.xPerY = f;
                c.squareness.atY   = f != 0 ? -offset / f : 0;
                squarenessAxis     = a.id;
                continue;
            } else {
                notes.push_back("axis " + a.name + " (" + kind + ") has no jplacer equivalent yet and was left out");
                continue;
            }
            c.axes.push_back(std::move(a));
        }
    }

    // Actuators first: nozzles name theirs.
    std::map<std::string, std::string> actuatorIdByName;
    std::map<std::string, std::string> pumpNames;   // head id -> its pump actuator's name
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
        // What on and off set it to, as OpenPnP's defaults.
        if (a.valueType == JPActuatorConfig::ValueType::Number) {
            a.onValue = x.attr("default-on-double");
            a.offValue = x.attr("default-off-double");
        } else if (a.valueType == JPActuatorConfig::ValueType::Text) {
            a.onValue = x.attr("default-on-string");
            a.offValue = x.attr("default-off-string");
        }
        // What it is switched to as the machine connects, homes and is let go.
        for (const auto& [attr, field] : { std::pair{ "enabled-actuation", &JPActuatorConfig::enabledActuation },
                                           std::pair{ "homed-actuation", &JPActuatorConfig::homedActuation },
                                           std::pair{ "disabled-actuation", &JPActuatorConfig::disabledActuation } })
            if (!x.attr(attr).empty()) a.*field = x.attr(attr);
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
            // A value to set it to: OpenPnP's double or string command.
            for (const char* kind : { "ACTUATE_DOUBLE_COMMAND", "ACTUATE_STRING_COMMAND" })
                if (const std::string* t = findCommand(cmds->second, kind, a.id); t && a.valueCommand.empty())
                    a.valueCommand = translate(*t, -1, "actuator " + a.name, notes);
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
        // OpenPnP's camera calibration is not carried over: jplacer measures
        // its cameras itself. Say so when a camera had one.
        if (const JPXmlElement* ac = x.child("advanced-calibration"); ac && ac->attr("enabled") == "true")
            notes.push_back("camera " + cam.name + ": OpenPnP's camera calibration is not imported; "
                            "calibrate the camera in jplacer");
        cam.device = JJson::object();
        cam.device["openpnpClass"] = shortClass(x);
        for (const char* key : { "unique-id", "format-id", "fps", "rotation", "flip-x", "flip-y", "light-actuator-id" })
            if (!x.attr(key).empty()) cam.device[key] = x.attr(key);
        // OpenPnpCaptureCamera's unique id is the device's own name and the
        // USB port it was on ("top: top usb-0000:00:14.0-8.2"). jplacer finds a
        // camera by its name alone, so a different port or hub does not lose it.
        if (shortClass(x) == "OpenPnpCaptureCamera") {
            const std::string& uid = x.attr("unique-id");
            const size_t usb = uid.rfind(" usb-");
            cam.device["backend"] = "v4l2";
            cam.device["name"]    = usb == std::string::npos ? uid : uid.substr(0, usb);
            if (const double fps = number(x.attr("fps")); fps > 0) cam.device["fps"] = fps;
            // When its light is switched.
            auto flag = [&x](const char* a, bool def) { return x.attr(a).empty() ? def : x.attr(a) == "true"; };
            cam.light.beforeCapture = flag("before-capture-light-on", cam.light.beforeCapture);
            cam.light.userAction    = flag("user-action-light-on", cam.light.userAction);
            cam.light.afterCapture  = flag("after-capture-light-off", cam.light.afterCapture);
            cam.light.antiGlare     = flag("anti-glare-light-off", cam.light.antiGlare);
            // White balance: each channel's balance and gamma.
            const char* channels[] = { "red", "green", "blue" };
            for (size_t ch = 0; ch < 3; ++ch) {
                const std::string c = channels[ch];
                if (!x.attr(c + "-balance").empty()) cam.whiteBalance.balance[ch] = number(x.attr(c + "-balance"));
                if (!x.attr(c + "-gamma").empty()) cam.whiteBalance.gamma[ch] = number(x.attr(c + "-gamma"));
            }
            if (x.child("red-color-map"))
                notes.push_back("camera " + cam.name + ": its mapped white balance is not brought in; balance it again "
                                "(Machine Setup, White Balance)");
            // Settling: how a picture for vision waits for the camera to stop.
            if (!x.attr("settle-method").empty()) {
                cam.settle.method = x.attr("settle-method") == "Motion" ? "Euclidean" : x.attr("settle-method");
                if (x.attr("settle-method") == "Motion")
                    notes.push_back("camera " + cam.name + ": settles by Euclidean difference; jplacer has no Motion settling");
            }
            if (!x.attr("settle-time-ms").empty()) cam.settle.timeMs = int(number(x.attr("settle-time-ms")));
            if (!x.attr("settle-timeout-ms").empty()) cam.settle.timeoutMs = int(number(x.attr("settle-timeout-ms")));
            if (!x.attr("settle-threshold").empty()) cam.settle.threshold = number(x.attr("settle-threshold"));
            if (!x.attr("settle-debounce").empty()) cam.settle.debounce = int(number(x.attr("settle-debounce")));
            if (!x.attr("settle-mask-circle").empty()) cam.settle.maskCircle = number(x.attr("settle-mask-circle"));
            // The camera's own settings, as OpenPnP set them (a value, or
            // automatic): jplacer sets them again each time it opens the camera.
            JJson controls = JJson::object();
            for (const char* name : { "exposure", "white-balance", "focus", "gain", "brightness", "hue", "contrast",
                                      "saturation", "gamma", "sharpness", "backlight-compensation", "power-line-frequency", "zoom" }) {
                const JPXmlElement* p = x.child(name);
                if (!p || (p->attr("value").empty() && !yes(p->attr("auto")))) continue;
                controls[name]["auto"] = yes(p->attr("auto"));
                if (!p->attr("value").empty()) controls[name]["value"] = number(p->attr("value"));
            }
            if (!controls.empty()) cam.device["controls"] = controls;
        } else {
            notes.push_back("camera " + cam.name + " (" + shortClass(x) + ") is not a kind jplacer can capture from yet");
        }
        c.cameras.push_back(std::move(cam));
    };

    // Nozzle tips before the nozzles, which name them. A tip's diameter is
    // the one OpenPnP's nozzle tip calibration finds it by. OpenPnP's tool
    // changer is four places, each optional, with a speed into each after
    // the first and an actuator switched on after each of the first three;
    // they become load steps, unloading being loading backwards, as in
    // OpenPnP. Its places are the nozzle's, in the axes' own coordinates,
    // as jplacer's steps are.
    std::set<std::string> tipIds;
    if (const JPXmlElement* tips = machine->child("nozzle-tips")) {
        for (const JPXmlElement& x : tips->children) {
            JPNozzleTipConfig t;
            t.id   = x.attr("id");
            t.name = x.attr("name");
            if (const JPXmlElement* cal = x.child("calibration")) {
                t.diameter = lengthChild(*cal, "calibration-tip-diameter");
                // Runout: how it is measured (the measurements are jplacer's own).
                t.runoutCalibration.enabled = yes(cal->attr("enabled"));
                if (const int n = int(number(cal->attr("angle-subdivisions"))); n > 0)
                    t.runoutCalibration.divisions = std::clamp(n, JPNozzleTipConfig::RunoutCalibration::kLeastDivisions,
                                                               JPNozzleTipConfig::RunoutCalibration::kMostDivisions);
                t.runoutCalibration.misdetects = int(number(cal->attr("allow-misdetections")));
                t.runoutCalibration.zOffset = lengthChild(*cal, "calibration-Z-offset");
            }
            t.pickDwellMs = int(number(x.attr("pick-dwell-milliseconds")));
            t.placeDwellMs = int(number(x.attr("place-dwell-milliseconds")));
            if (x.child("max-part-diameter")) t.maxPartDiameterMm = lengthChild(x, "max-part-diameter");
            if (x.child("max-pick-tolerance")) t.maxPickToleranceMm = lengthChild(x, "max-pick-tolerance");
            // Part detection by the vacuum.
            auto text = [&x](const char* child) { const JPXmlElement* e = x.child(child); return e ? e->text : std::string(); };
            auto sensing = [&](JPNozzleTipConfig::Sensing& s, const std::string& on) {
                if (const std::string m = text(("method-part-" + on).c_str()); !m.empty()) s.method = m;
                s.low      = number(text(("vacuum-level-part-" + on + "-low").c_str()));
                s.high     = number(text(("vacuum-level-part-" + on + "-high").c_str()));
                s.diffLow  = number(text(("vacuum-difference-part-" + on + "-low").c_str()));
                s.diffHigh = number(text(("vacuum-difference-part-" + on + "-high").c_str()));
            };
            sensing(t.partOn, "on");
            sensing(t.partOff, "off");
            t.partOffProbingMs = int(number(x.attr("part-off-probing-milliseconds")));
            t.partOffDwellMs = int(number(x.attr("part-off-dwell-milliseconds")));
            const struct { const char* place; const char* speed; const char* actuator; } changer[] = {
                { "changer-start-location", nullptr, "changer-actuator-post-step-one" },
                { "changer-mid-location", "changer-start-to-mid-speed", "changer-actuator-post-step-two" },
                { "changer-mid-location-2", "changer-mid-to-mid-2-speed", "changer-actuator-post-step-three" },
                { "changer-end-location", "changer-mid-2-to-end-speed", nullptr },
            };
            for (const auto& step : changer) {
                const std::optional<JPMachineLocation> at = location(x, step.place);
                if (at && (at->x != 0 || at->y != 0 || at->z != 0 || at->rotation != 0)) {   // all 0: not set
                    JPChangerStep m;
                    m.x = at->x;
                    m.y = at->y;
                    m.z = at->z;
                    m.rotation = at->rotation;
                    if (step.speed)
                        if (const JPXmlElement* sp = x.child(step.speed)) m.speed = number(sp->text);
                    t.loadSteps.push_back(m);
                }
                if (step.actuator)
                    if (const JPXmlElement* a = x.child(step.actuator); a && !a->text.empty()) {
                        JPChangerStep s;
                        s.kind = JPChangerStep::Kind::Actuator;
                        s.actuatorId = a->text;   // a name until the actuators are read
                        t.loadSteps.push_back(s);
                    }
            }
            tipIds.insert(t.id);
            c.nozzleTips.push_back(std::move(t));
        }
    }

    if (const JPXmlElement* heads = machine->child("heads")) {
        for (const JPXmlElement& h : heads->children) {
            JPHeadConfig head;
            head.id                    = h.attr("id");
            head.name                  = h.attr("name");
            head.homingFiducial        = location(h, "homing-fiducial-location");
            head.visualHoming          = h.attr("visual-homing-method") == "ResetToFiducialLocation";
            head.park                  = location(h, "park-location");
            head.rigPrimary            = location(h, "calibration-primary-fiducial-location");
            head.rigSecondary          = location(h, "calibration-secondary-fiducial-location");
            head.rigPrimaryDiameter    = lengthChild(h, "calibration-primary-fiducial-diameter");
            head.rigSecondaryDiameter  = lengthChild(h, "calibration-secondary-fiducial-diameter");
            head.rigTestObjectDiameter = lengthChild(h, "calibration-test-object-diameter");
            // OpenPnP's homing mark is a part (FIDUCIAL-HOME) in another file;
            // where the rig's primary fiducial sits on it, that is its size.
            if (head.homingFiducial && head.rigPrimary
                && std::abs(head.homingFiducial->x - head.rigPrimary->x) < 0.01
                && std::abs(head.homingFiducial->y - head.rigPrimary->y) < 0.01)
                head.homingFiducialDiameter = head.rigPrimaryDiameter;
            head.pumpControl           = h.attr("vacuum-pump-control");
            head.pumpOnWaitMs          = int(number(h.attr("pump-on-wait-milliseconds")));
            if (const JPXmlElement* pump = h.child("pump-actuator-name")) pumpNames[head.id] = pump->text;
            if (!h.attr("visual-homing-method").empty() && h.attr("visual-homing-method") != "None"
                && h.attr("visual-homing-method") != "ResetToFiducialLocation")
                notes.push_back("head " + head.name + ": visual homing method " + h.attr("visual-homing-method")
                                + " is not one jplacer does; it resets to the fiducial location");
            if (head.visualHoming)
                notes.push_back("head " + head.name + ": homing finishes by finding the homing fiducial with the "
                                "camera, as in OpenPnP, once the camera on the head is calibrated in jplacer (Calibrate); "
                                "until then the axes take their home coordinates from the switches alone");
            c.heads.push_back(std::move(head));
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
                    // Its vacuum's actuators, named in OpenPnP; kept by id.
                    auto actuator = [&](const char* child, std::string& id) {
                        if (const JPXmlElement* v = x.child(child)) {
                            const auto it = actuatorIdByName.find(v->text);
                            if (it != actuatorIdByName.end()) id = it->second;
                        }
                    };
                    actuator("vacuum-actuator-name", n.vacuumActuatorId);
                    actuator("blow-off-actuator-name", n.blowOffActuatorId);
                    actuator("vacuum-sense-actuator-name", n.vacuumSenseActuatorId);
                    n.blowOffClosesVacuum = x.attr("blow-off-closing-valve") == "true";
                    n.pickDwellMs = int(number(x.attr("pick-dwell-milliseconds")));
                    n.placeDwellMs = int(number(x.attr("place-dwell-milliseconds")));
                    // OpenPnP keeps the ids of tips since deleted in a
                    // nozzle's list; only tips the machine has are kept.
                    if (const JPXmlElement* fit = x.child("compatible-nozzle-tip-ids"))
                        for (const JPXmlElement& t : fit->children)
                            if (tipIds.count(t.text)) n.tipIds.push_back(t.text);
                    if (n.fits(x.attr("current-nozzle-tip-id"))) n.tipId = x.attr("current-nozzle-tip-id");
                    if (x.attr("changer-enabled") != "true" && !n.tipIds.empty())
                        notes.push_back("nozzle " + n.name + ": OpenPnP changes its tips by hand; in jplacer a tip "
                                        "is changed by its own load and unload steps, by hand when it has none");
                    c.nozzles.push_back(std::move(n));
                }
            }
        }
    }
    if (const JPXmlElement* acts = machine->child("actuators"))
        for (const JPXmlElement& x : acts->children) addActuator(x, std::string());
    if (const JPXmlElement* cams = machine->child("cameras"))
        for (const JPXmlElement& x : cams->children) addCamera(x, std::string());

    if (squarenessAxis) {
        auto onInput = [&](JPMountConfig& m) { if (m.axisX == *squarenessAxis) m.axisX = c.squareness.axisX; };
        for (JPNozzleConfig& n : c.nozzles)     onInput(n.mount);
        for (JPCameraConfig& m : c.cameras)     onInput(m.mount);
        for (JPActuatorConfig& a : c.actuators) onInput(a.mount);
    }
    for (JPNozzleTipConfig& t : c.nozzleTips)
        for (JPChangerStep& s : t.loadSteps) {
            if (s.kind != JPChangerStep::Kind::Actuator) continue;
            const auto a = actuatorIdByName.find(s.actuatorId);
            if (a != actuatorIdByName.end()) {
                s.actuatorId = a->second;
            } else {
                notes.push_back("nozzle tip " + t.name + ": its changer switches " + s.actuatorId + ", not an actuator here");
                s.actuatorId.clear();
            }
        }
    for (JPHeadConfig& h : c.heads)
        if (const auto p = pumpNames.find(h.id); p != pumpNames.end())
            if (const auto a = actuatorIdByName.find(p->second); a != actuatorIdByName.end()) h.pumpActuatorId = a->second;
    for (const std::string& p : c.problems()) notes.push_back(p);
    JLOGC(JPlacerLog::kImport, JLogLevel::Info) << machineXml << ": " << c.drivers.size() << " controller(s), "
        << c.axes.size() << " axes, " << c.nozzles.size() << " nozzle(s), " << c.nozzleTips.size() << " nozzle tip(s), "
        << c.cameras.size() << " camera(s), "
        << c.actuators.size() << " actuator(s)";
    for (const std::string& n : notes) JLOGC(JPlacerLog::kImport, JLogLevel::Warn) << n;
    cell = std::move(c);
    return true;
}

void JPOpenPnpMachineImporter::keepFrom(const JPCellConfig& previous, JPCellConfig& cell) {
    // The port chosen here, by its permanent name: OpenPnP's file still names
    // the port it had, which may be a different device now.
    for (JPDriverConfig& d : cell.drivers)
        if (const JPDriverConfig* was = previous.driver(d.id); was && was->link["type"].str() == d.link["type"].str()
            && !was->link["port"].str().empty()) {
            JLOGC(JPlacerLog::kImport, JLogLevel::Info) << "controller " << d.name << " keeps " << was->link["port"].str();
            d.link["port"] = was->link["port"].str();
        }
    // What jplacer measured or was taught itself is not OpenPnP's to
    // replace: each camera's calibrations and how much of a straightened
    // picture it shows, the squareness, and the nozzle tips' changer steps.
    for (JPCameraConfig& cam : cell.cameras)
        for (const JPCameraConfig& was : previous.cameras)
            if (was.id == cam.id) {
                cam.calibrations = was.calibrations;
                cam.showAll = was.showAll;
            }
    if (previous.squareness.active()) cell.squareness = previous.squareness;
    // Which tip is on each nozzle is known here (set by hand, or by
    // loading): OpenPnP's file says what it last believed, which a hand
    // since may have changed. A wrong tip is a crash; it is never taken.
    for (JPNozzleConfig& n : cell.nozzles) {
        n.tipId.clear();
        for (const JPNozzleConfig& was : previous.nozzles)
            if (was.id == n.id) {
                if (n.fits(was.tipId)) n.tipId = was.tipId;
                n.homeCommand = was.homeCommand;   // OpenPnP has no Z-only home
            }
    }
    for (JPNozzleTipConfig& tip : cell.nozzleTips)
        for (const JPNozzleTipConfig& was : previous.nozzleTips)
            if (was.id == tip.id) {
                tip.loadSteps = was.loadSteps;
                tip.unloadReversesLoad = was.unloadReversesLoad;
                tip.unloadSteps = was.unloadSteps;
                tip.runout = was.runout;   // measured here, not in OpenPnP
            }
}

} // inline namespace jf
