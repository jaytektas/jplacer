// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPScripting.h"
#include "JPScriptProcess.h"

#include "common/JPlacerLog.h"

#include <j/core/Log.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>

extern char** environ;

inline namespace jf {

namespace fs = std::filesystem;

namespace {

// The helper modules a script imports to ask the machine (JPScripting::api).
constexpr const char* kPythonModule = R"PY(# jplacer's machine, for the scripts jplacer runs (as OpenPnP's scripts have `machine`).
# Written by jplacer; changes to it are lost.
#
#   import jplacer
#   jplacer.move_to("N1", x=10, y=20)            # a nozzle, camera or actuator by name
#   jplacer.actuate("Vacuum", True)
#   print(jplacer.read("Vacuum Sense"), jplacer.location("Top"), jplacer.globals, jplacer.event)
import json, os

globals = json.loads(os.environ.get("JPLACER_GLOBALS", "{}"))
event = os.environ.get("JPLACER_EVENT", "")
_request, _answer = (int(f) for f in os.environ.get("JPLACER_API", "3,4").split(","))
_answers = os.fdopen(_answer, "r")

class Error(Exception):
    pass

def call(name, **arguments):
    """Ask the machine; the answer, or Error with why."""
    arguments["call"] = name
    os.write(_request, (json.dumps(arguments, default=str) + "\n").encode())
    answer = json.loads(_answers.readline())
    if "error" in answer:
        raise Error(answer["error"])
    return answer.get("result")

def positions():                                    return call("positions")
def location(tool):                                 return call("location", tool=tool)
def move_to(tool, x=None, y=None, z=None, rotation=None, speed=1.0, straight=False):
    return call("moveTo", tool=tool, x=x, y=y, z=z, rotation=rotation, speed=speed, straight=straight)
def safe_z(head=None, speed=1.0):                   return call("safeZ", head=head, speed=speed)
def home():                                         return call("home")
def actuate(actuator, value):                       return call("actuate", actuator=actuator, value=value)
def read(actuator, parameter=None):                 return call("read", actuator=actuator, parameter=parameter)
def gcode(line, controller=None):                   return call("gcode", line=line, controller=controller)
def message(text):                                  return call("message", text=text)
)PY";

constexpr const char* kNodeModule = R"JS(// jplacer's machine, for the scripts jplacer runs (as OpenPnP's scripts have `machine`).
// Written by jplacer; changes to it are lost.
//
//   const jplacer = require("jplacer");
//   jplacer.moveTo("N1", { x: 10, y: 20 });
const fs = require("fs");
const [request, answer] = (process.env.JPLACER_API || "3,4").split(",").map(Number);
let pending = "";

function call(name, args = {}) {
    fs.writeSync(request, JSON.stringify(Object.assign({ call: name }, args)) + "\n");
    const buf = Buffer.alloc(65536);
    while (!pending.includes("\n")) {
        const n = fs.readSync(answer, buf, 0, buf.length, null);
        if (n <= 0) throw new Error("jplacer did not answer");
        pending += buf.toString("utf8", 0, n);
    }
    const nl = pending.indexOf("\n");
    const reply = JSON.parse(pending.slice(0, nl));
    pending = pending.slice(nl + 1);
    if (reply.error) throw new Error(reply.error);
    return reply.result;
}

module.exports = {
    globals: JSON.parse(process.env.JPLACER_GLOBALS || "{}"),
    event: process.env.JPLACER_EVENT || "",
    call,
    positions: () => call("positions"),
    location: (tool) => call("location", { tool }),
    moveTo: (tool, to = {}) => call("moveTo", Object.assign({ tool }, to)),
    safeZ: (head, speed = 1) => call("safeZ", { head, speed }),
    home: () => call("home"),
    actuate: (actuator, value) => call("actuate", { actuator, value }),
    read: (actuator, parameter) => call("read", { actuator, parameter }),
    gcode: (line, controller) => call("gcode", { line, controller }),
    message: (text) => call("message", { text }),
};
)JS";

// OpenPnP's scripting objects for OpenPnP's Python scripts (machine, config,
// scripting, gui), over the jplacer module.
constexpr const char* kOpenPnpModel = R"PY(# OpenPnP's scripting objects, for OpenPnP's Python scripts, over jplacer's machine.
# Written by jplacer; changes to it are lost.
#
# An OpenPnP script finds `machine`, `config`, `gui` and `scripting` as it does
# in OpenPnP, and imports org.openpnp.model (Location, LengthUnit) and
# org.openpnp.util.UiUtils (submitUiMachineTask); what it asks of them goes to
# jplacer (the jplacer module). Java's bean getters work both ways, as in
# Jython: nozzle.location and nozzle.getLocation().
import builtins
import os

import jplacer


class LengthUnit(object):
    Millimeters = "Millimeters"
    Centimeters = "Centimeters"
    Meters = "Meters"
    Inches = "Inches"
    Feet = "Feet"
    Mils = "Mils"
    Microns = "Microns"
    _mm = {"Millimeters": 1.0, "Centimeters": 10.0, "Meters": 1000.0, "Inches": 25.4, "Feet": 304.8,
           "Mils": 0.0254, "Microns": 0.001}
    _short = {"Millimeters": "mm", "Centimeters": "cm", "Meters": "m", "Inches": "in", "Feet": "ft",
              "Mils": "mil", "Microns": "um"}


class Bean(object):
    """Java bean access: getFoo() and isFoo() for the attribute foo, and the other way round."""

    def __getattr__(self, name):
        for prefix in ("get", "is"):
            if name.startswith(prefix) and len(name) > len(prefix) and name[len(prefix)].isupper():
                field = name[len(prefix)].lower() + name[len(prefix) + 1:]
                value = object.__getattribute__(self, field)
                return value if callable(value) else (lambda: value)
        raise AttributeError(name)


class Location(Bean):
    """OpenPnP's Location: x, y, z and rotation in units."""

    def __init__(self, units=LengthUnit.Millimeters, x=0.0, y=0.0, z=0.0, rotation=0.0):
        self.units, self.x, self.y, self.z, self.rotation = units, float(x), float(y), float(z), float(rotation)

    def convertToUnits(self, units):
        f = LengthUnit._mm[self.units] / LengthUnit._mm[units]
        return Location(units, self.x * f, self.y * f, self.z * f, self.rotation)

    def _other(self, l):
        return l.convertToUnits(self.units)

    def add(self, l):
        o = self._other(l)
        return Location(self.units, self.x + o.x, self.y + o.y, self.z + o.z, self.rotation + o.rotation)

    def subtract(self, l):
        o = self._other(l)
        return Location(self.units, self.x - o.x, self.y - o.y, self.z - o.z, self.rotation - o.rotation)

    def multiply(self, x, y=None, z=None, rotation=None):
        y, z, rotation = (x, x, x) if y is None else (y, z, rotation)
        return Location(self.units, self.x * x, self.y * y, self.z * z, self.rotation * rotation)

    def derive(self, x=None, y=None, z=None, rotation=None):
        pick = lambda v, w: w if v is None else v
        return Location(self.units, pick(x, self.x), pick(y, self.y), pick(z, self.z), pick(rotation, self.rotation))

    def getLinearDistanceTo(self, l):
        o = self._other(l)
        return ((self.x - o.x) ** 2 + (self.y - o.y) ** 2) ** 0.5

    def getXyzDistanceTo(self, l):
        o = self._other(l)
        return ((self.x - o.x) ** 2 + (self.y - o.y) ** 2 + (self.z - o.z) ** 2) ** 0.5

    def toString(self):
        return "(%f, %f, %f, %f %s)" % (self.x, self.y, self.z, self.rotation, LengthUnit._short[self.units])

    __str__ = toString
    __repr__ = toString


def _mm(location):
    return location.convertToUnits(LengthUnit.Millimeters)


class Part(Bean):
    def __init__(self, d):
        self.id, self.name, self.packageId, self.height = d["id"], d["name"], d.get("package", ""), d.get("height", 0)

    def __str__(self):
        return self.id


def _head_of(head_id):
    for h in machine.heads:
        if h.id == head_id:
            return h
    return None


class HeadMountable(Bean):
    def __init__(self, d, head_id=None):
        self.id, self.name, self._head = d["id"], d["name"], head_id

    @property
    def location(self):
        l = jplacer.location(self.id)
        v = lambda k: l.get(k) or 0.0
        return Location(LengthUnit.Millimeters, v("x"), v("y"), v("z"), v("rotation"))

    @property
    def head(self):
        return _head_of(self._head)

    def moveTo(self, location, speed=1.0):
        l = _mm(location)
        jplacer.move_to(self.id, x=l.x, y=l.y, z=l.z, rotation=l.rotation, speed=speed)

    def moveToSafeZ(self, speed=1.0):
        jplacer.safe_z(self._head, speed=speed)

    def __str__(self):
        return self.name


class Nozzle(HeadMountable):
    def __init__(self, d, head_id):
        HeadMountable.__init__(self, d, head_id)
        self.nozzleTipId = d.get("tip", "")

    @property
    def part(self):
        for h in jplacer.call("machine")["heads"]:
            for n in h["nozzles"]:
                if n["id"] == self.id and n["part"]:
                    return config.getPart(n["part"])
        return None

    def pick(self, part=None):
        jplacer.call("pick", nozzle=self.id, part=part.id if part is not None else None)

    def place(self):
        jplacer.call("place", nozzle=self.id)


class Camera(HeadMountable):
    def __init__(self, d, head_id=None):
        HeadMountable.__init__(self, d, head_id)
        self.looking = d.get("looking", "Down")


class Actuator(HeadMountable):
    def actuate(self, value):
        jplacer.actuate(self.id, value)

    def read(self, parameter=None):
        return jplacer.read(self.id, parameter)


class Head(Bean):
    def __init__(self, d):
        self.id, self.name = d["id"], d["name"]
        self.nozzles = [Nozzle(n, self.id) for n in d["nozzles"]]
        self.cameras = [Camera(c, self.id) for c in d["cameras"]]
        self.actuators = [Actuator(a, self.id) for a in d["actuators"]]

    @property
    def defaultNozzle(self):
        return self.nozzles[0] if self.nozzles else None

    @property
    def defaultCamera(self):
        return self.cameras[0] if self.cameras else None

    def getActuatorByName(self, name):
        return next((a for a in self.actuators if a.name == name), None)

    def getNozzle(self, id):
        return next((n for n in self.nozzles if n.id == id), None)

    def isCarryingPart(self):
        return any(n.part is not None for n in self.nozzles)

    def moveToSafeZ(self, speed=1.0):
        jplacer.safe_z(self.id, speed=speed)

    def __str__(self):
        return self.name


class Feeder(Bean):
    def __init__(self, d):
        self.id, self.name, self.enabled, self._part = d["id"], d["name"], d["enabled"], d["part"]
        self._feedCount = int(d["feedCount"])

    @property
    def part(self):
        return config.getPart(self._part)

    def getFeedCount(self):
        return self._feedCount

    def setFeedCount(self, count):
        jplacer.call("setFeedCount", feeder=self.id, count=int(count))
        self._feedCount = int(count)

    def __str__(self):
        return self.name


class Machine(Bean):
    """OpenPnP's machine, as it is now (read afresh each time it is asked for)."""

    def _now(self):
        return jplacer.call("machine")

    @property
    def name(self):
        return self._now()["name"]

    @property
    def heads(self):
        return [Head(h) for h in self._now()["heads"]]

    @property
    def defaultHead(self):
        heads = self.heads
        return heads[0] if heads else None

    @property
    def cameras(self):
        return [Camera(c) for c in self._now()["cameras"]]

    @property
    def actuators(self):
        return [Actuator(a) for a in self._now()["actuators"]]

    @property
    def feeders(self):
        return [Feeder(f) for f in self._now()["feeders"]]

    def getActuatorByName(self, name):
        for a in self.actuators + [a for h in self.heads for a in h.actuators]:
            if a.name == name:
                return a
        return None

    def getFeeder(self, id):
        return next((f for f in self.feeders if f.id == id), None)

    def getFeederByName(self, name):
        return next((f for f in self.feeders if f.name == name), None)

    def home(self):
        jplacer.home()

    def __str__(self):
        return "Machine " + self.name


class Configuration(Bean):
    """OpenPnP's configuration: its machine and parts."""

    @property
    def machine(self):
        return machine

    @property
    def parts(self):
        return [Part(p) for p in jplacer.call("machine")["parts"]]

    def getPart(self, id):
        return next((p for p in self.parts if p.id == id), None)

    def __str__(self):
        return "Configuration"


class _Path(object):
    def __init__(self, path):
        self._path = path

    def toString(self):
        return self._path

    __str__ = toString


class Scripting(Bean):
    def getScriptsDirectory(self):
        return _Path(os.environ.get("JPLACER_SCRIPTS", "") + os.sep)

    def on(self, event, globals=None):
        """An event's scripts run, with these globals."""
        jplacer.call("scriptingOn", event=str(event), globals=globals or {})

    def execute(self, script, globals=None):
        """A script run (its path, or one in the scripts folder), with these globals."""
        jplacer.call("scriptingExecute", path=str(script), globals=globals or {})

    def __str__(self):
        return "Scripting"


class BoardLocation(Bean):
    """A board or panel in the job, as OpenPnP's BoardLocation."""

    def __init__(self, index, d):
        self._index, self.id, self.name, self.side = index, d["id"], d["name"], d["side"]
        self.enabled = d["enabled"]
        l = d["location"]
        self.location = Location(LengthUnit.Millimeters, l["x"], l["y"], l["z"], l["rotation"])

    def setEnabled(self, enabled):
        jplacer.call("setBoardEnabled", board=self._index, enabled=bool(enabled))
        self.enabled = bool(enabled)

    def __str__(self):
        return self.name


class Job(Bean):
    @property
    def boardLocations(self):
        return [BoardLocation(i, b) for i, b in enumerate(jplacer.call("job")["boards"])]

    @property
    def file(self):
        return jplacer.call("job")["file"]


class JobTab(Bean):
    @property
    def job(self):
        return Job()

    def refresh(self):
        jplacer.call("refreshJob")


class Gui(Bean):
    """OpenPnP's main window, as far as scripts use it: the Job tab."""

    def __init__(self):
        self.jobTab = JobTab()

    def getCameraViews(self):
        return _CameraViews()

    def __str__(self):
        return "MainFrame"


class _Model(Bean):
    """A pipeline result's model, as OpenPnP's scripts read it (attributes, and Java's getters)."""

    def __init__(self, d):
        for k, v in d.items():
            setattr(self, k, _model(v))

    def __repr__(self):
        return repr(self.__dict__)


def _model(v):
    if isinstance(v, dict):
        return _Model(v)
    if isinstance(v, list):
        return [_model(x) for x in v]
    return v


class _Result(Bean):
    def __init__(self, d):
        self.model, self._text = _model(d.get("value")), d.get("text", "")

    def toString(self):
        return self._text

    __str__ = toString


class CvPipeline(Bean):
    """OpenPnP's CvPipeline, run by jplacer on the head camera where it is."""

    def __init__(self, xml="<cv-pipeline><stages/></cv-pipeline>"):
        self._xml, self._properties, self._results = xml, {}, {}

    def setProperty(self, name, value):
        self._properties[name] = value

    def getProperty(self, name):
        return self._properties.get(name)

    def process(self):
        camera = self._properties.get("camera")   # the camera it looks with (none: the head camera)
        self._results = jplacer.call("pipeline", xml=self._xml, camera=camera.id if camera is not None else None)["results"]

    def getResult(self, name):
        r = self._results.get(str(name))
        return _Result(r) if r is not None else None

    def getExpectedResult(self, name):
        r = self.getResult(name)
        if r is None:
            raise Exception("Pipeline result stage \"%s\" not found." % name)
        return r

    def getWorkingImage(self):
        return _WorkingImage()

    def toXmlString(self):
        return self._xml


class _WorkingImage(object):
    """The last pipeline's working image (kept by jplacer to show)."""


class OpenCvUtils(object):
    @staticmethod
    def toBufferedImage(image):
        return image


class _CameraView(object):
    def showFilteredImage(self, image, text="", milliseconds=1500):
        jplacer.call("showPipelineImage", ms=int(milliseconds), text=str(text))


class _CameraViews(object):
    def getCameraView(self, camera):
        return _CameraView()


class Utils2D(object):
    @staticmethod
    def calculateBoardPlacementLocation(board, location=None):
        l = _mm(location if location is not None else Location())
        r = jplacer.call("boardPlacementLocation", board=board._index,
                         location={"x": l.x, "y": l.y, "z": l.z, "rotation": l.rotation})
        return Location(LengthUnit.Millimeters, r["x"], r["y"], r["z"], r["rotation"])


class VisionUtils(object):
    @staticmethod
    def readQrCode(camera):
        return jplacer.call("readQrCode", camera=camera.id)


machine = Machine()
config = Configuration()
scripting = Scripting()
gui = Gui()


def install():
    """OpenPnP's globals for the script about to run."""
    builtins.machine = machine
    builtins.config = config
    builtins.scripting = scripting
    builtins.gui = gui
    for name, value in jplacer.globals.items():   # what the script is run for (an event's globals)
        setattr(builtins, name, value)
)PY";

// OpenPnP's scripting objects for OpenPnP's JavaScript scripts (as Nashorn has
// them), over the jplacer module.
constexpr const char* kOpenPnpModelJs = R"JS(// OpenPnP's scripting objects, for OpenPnP's JavaScript scripts, over jplacer's machine.
// Written by jplacer; changes to it are lost.
//
// OpenPnP runs its scripts in Java's own JavaScript (Nashorn); jplacer runs
// them under node in a context that has what Nashorn gives them: print, load,
// Packages, JavaImporter (for `with`), `for each`, the Java packages OpenPnP's
// scripts use (as far as jplacer has them), and OpenPnP's machine, config,
// scripting and gui. Java's bean getters work both ways: nozzle.location and
// nozzle.getLocation().
"use strict";
const fs = require("fs");
const vm = require("vm");
const jplacer = require("jplacer");

// getFoo() (isFoo() for a boolean) beside each property foo of a class.
function beans(cls, names, bool = []) {
    for (const n of names) {
        const getter = (bool.includes(n) ? "is" : "get") + n[0].toUpperCase() + n.slice(1);
        if (!(getter in cls.prototype))
            Object.defineProperty(cls.prototype, getter, { value: function () { return this[n]; } });
    }
}

const LengthUnit = {
    Millimeters: "Millimeters", Centimeters: "Centimeters", Meters: "Meters", Inches: "Inches", Feet: "Feet",
    Mils: "Mils", Microns: "Microns",
};
const mmPer = { Millimeters: 1, Centimeters: 10, Meters: 1000, Inches: 25.4, Feet: 304.8, Mils: 0.0254, Microns: 0.001 };
const shortName = { Millimeters: "mm", Centimeters: "cm", Meters: "m", Inches: "in", Feet: "ft", Mils: "mil", Microns: "um" };

class Location {
    constructor(units = LengthUnit.Millimeters, x = 0, y = 0, z = 0, rotation = 0) {
        Object.assign(this, { units, x: +x, y: +y, z: +z, rotation: +rotation });
    }
    convertToUnits(units) {
        const f = mmPer[this.units] / mmPer[units];
        return new Location(units, this.x * f, this.y * f, this.z * f, this.rotation);
    }
    add(l) {
        const o = l.convertToUnits(this.units);
        return new Location(this.units, this.x + o.x, this.y + o.y, this.z + o.z, this.rotation + o.rotation);
    }
    subtract(l) {
        const o = l.convertToUnits(this.units);
        return new Location(this.units, this.x - o.x, this.y - o.y, this.z - o.z, this.rotation - o.rotation);
    }
    derive(x, y, z, rotation) {
        const pick = (v, w) => (v === null || v === undefined ? w : v);
        return new Location(this.units, pick(x, this.x), pick(y, this.y), pick(z, this.z), pick(rotation, this.rotation));
    }
    getLinearDistanceTo(l) {
        const o = l.convertToUnits(this.units);
        return Math.hypot(this.x - o.x, this.y - o.y);
    }
    toString() {
        const f = (v) => v.toFixed(6);
        return `(${f(this.x)}, ${f(this.y)}, ${f(this.z)}, ${f(this.rotation)} ${shortName[this.units]})`;
    }
}
beans(Location, ["units", "x", "y", "z", "rotation"]);

const now = () => jplacer.call("machine");

class Part {
    constructor(d) { Object.assign(this, { id: d.id, name: d.name, packageId: d.package || "", height: d.height || 0 }); }
    toString() { return this.id; }
}
beans(Part, ["id", "name", "height"]);

class HeadMountable {
    constructor(d, headId) { Object.assign(this, { id: d.id, name: d.name, headId }); }
    get location() {
        const l = jplacer.location(this.id);
        return new Location(LengthUnit.Millimeters, l.x || 0, l.y || 0, l.z || 0, l.rotation || 0);
    }
    get head() { return machine.heads.find((h) => h.id === this.headId) || null; }
    moveTo(location, speed = 1) {
        const l = location.convertToUnits(LengthUnit.Millimeters);
        jplacer.moveTo(this.id, { x: l.x, y: l.y, z: l.z, rotation: l.rotation, speed });
    }
    moveToSafeZ(speed = 1) { jplacer.safeZ(this.headId, speed); }
    toString() { return this.name; }
}
beans(HeadMountable, ["id", "name", "location", "head"]);

class Nozzle extends HeadMountable {
    get part() {
        for (const h of now().heads)
            for (const n of h.nozzles) if (n.id === this.id && n.part) return config.getPart(n.part);
        return null;
    }
    pick(part) { jplacer.call("pick", { nozzle: this.id, part: part ? part.id : null }); }
    place() { jplacer.call("place", { nozzle: this.id }); }
}
beans(Nozzle, ["part"]);

class Camera extends HeadMountable {
    constructor(d, headId) { super(d, headId); this.looking = d.looking || "Down"; }
}
beans(Camera, ["looking"]);

class Actuator extends HeadMountable {
    actuate(value) { jplacer.actuate(this.id, value); }
    read(parameter) { return jplacer.read(this.id, parameter); }
}

class Head {
    constructor(d) {
        Object.assign(this, { id: d.id, name: d.name });
        this.nozzles = d.nozzles.map((n) => new Nozzle(n, d.id));
        this.cameras = d.cameras.map((c) => new Camera(c, d.id));
        this.actuators = d.actuators.map((a) => new Actuator(a, d.id));
    }
    get defaultNozzle() { return this.nozzles[0] || null; }
    get defaultCamera() { return this.cameras[0] || null; }
    getActuatorByName(name) { return this.actuators.find((a) => a.name === name) || null; }
    getNozzle(id) { return this.nozzles.find((n) => n.id === id) || null; }
    isCarryingPart() { return this.nozzles.some((n) => n.part !== null); }
    moveToSafeZ(speed = 1) { jplacer.safeZ(this.id, speed); }
    toString() { return this.name; }
}
beans(Head, ["id", "name", "nozzles", "cameras", "actuators", "defaultNozzle", "defaultCamera"]);

class Feeder {
    constructor(d) { Object.assign(this, { id: d.id, name: d.name, enabled: d.enabled, partId: d.part, feedCount: d.feedCount }); }
    get part() { return config.getPart(this.partId); }
    setFeedCount(count) {
        jplacer.call("setFeedCount", { feeder: this.id, count: Math.trunc(count) });
        this.feedCount = Math.trunc(count);
    }
    toString() { return this.name; }
}
beans(Feeder, ["id", "name", "part", "feedCount"]);
beans(Feeder, ["enabled"], ["enabled"]);

class Machine {
    get name() { return now().name; }
    get heads() { return now().heads.map((h) => new Head(h)); }
    get defaultHead() { return this.heads[0] || null; }
    get cameras() { return now().cameras.map((c) => new Camera(c)); }
    get actuators() { return now().actuators.map((a) => new Actuator(a)); }
    get feeders() { return now().feeders.map((f) => new Feeder(f)); }
    getActuatorByName(name) {
        return this.actuators.concat(...this.heads.map((h) => h.actuators)).find((a) => a.name === name) || null;
    }
    getFeeder(id) { return this.feeders.find((f) => f.id === id) || null; }
    getFeederByName(name) { return this.feeders.find((f) => f.name === name) || null; }
    home() { jplacer.home(); }
    toString() { return "Machine " + this.name; }
}
beans(Machine, ["name", "heads", "defaultHead", "cameras", "actuators", "feeders"]);

class Configuration {
    get machine() { return machine; }
    get parts() { return now().parts.map((p) => new Part(p)); }
    getPart(id) { return this.parts.find((p) => p.id === id) || null; }
    toString() { return "Configuration"; }
}
beans(Configuration, ["machine", "parts"]);

class Scripting {
    getScriptsDirectory() {
        const dir = (process.env.JPLACER_SCRIPTS || "") + "/";
        return { toString: () => dir };
    }
    // An event's scripts run, with these globals.
    on(event, globals = {}) { jplacer.call("scriptingOn", { event: String(event), globals }); }
    // A script run (its path, or one in the scripts folder), with these globals.
    execute(script, globals = {}) { jplacer.call("scriptingExecute", { path: String(script), globals }); }
    toString() { return "Scripting"; }
}

class BoardLocation {
    constructor(index, d) {
        Object.assign(this, { index, id: d.id, name: d.name, side: d.side, enabled: d.enabled });
        const l = d.location;
        this.location = new Location(LengthUnit.Millimeters, l.x, l.y, l.z, l.rotation);
    }
    setEnabled(enabled) {
        jplacer.call("setBoardEnabled", { board: this.index, enabled: !!enabled });
        this.enabled = !!enabled;
    }
    toString() { return this.name; }
}
beans(BoardLocation, ["id", "name", "side", "location"]);
beans(BoardLocation, ["enabled"], ["enabled"]);

class Job {
    get boardLocations() { return jplacer.call("job").boards.map((b, i) => new BoardLocation(i, b)); }
    get file() { return jplacer.call("job").file; }
}
beans(Job, ["boardLocations", "file"]);

class JobTab {
    get job() { return new Job(); }
    refresh() { jplacer.call("refreshJob"); }
}
beans(JobTab, ["job"]);

// OpenPnP's main window, as far as scripts use it: the Job tab.
class Gui {
    constructor() { this.jobTab = new JobTab(); }
    getCameraViews() { return cameraViews; }
    toString() { return "MainFrame"; }
}
beans(Gui, ["jobTab"]);

// OpenPnP's CvPipeline, run by jplacer on the head camera where it is.
class CvPipeline {
    constructor(xml = "<cv-pipeline><stages/></cv-pipeline>") { Object.assign(this, { xml, properties: {}, results: {} }); }
    setProperty(name, value) { this.properties[name] = value; }
    getProperty(name) { return this.properties[name]; }
    process() {
        const camera = this.properties.camera;   // the camera it looks with (none: the head camera)
        this.results = jplacer.call("pipeline", { xml: this.xml, camera: camera ? camera.id : null }).results;
    }
    getResult(name) {
        const r = this.results[String(name)];
        return r ? { model: r.value, getModel: () => r.value, toString: () => r.text } : null;
    }
    getExpectedResult(name) {
        const r = this.getResult(name);
        if (!r) throw new Error(`Pipeline result stage "${name}" not found.`);
        return r;
    }
    getWorkingImage() { return { toString: () => "working image" }; }
    toXmlString() { return this.xml; }
}
const OpenCvUtils = { toBufferedImage: (image) => image };
const cameraViews = {
    getCameraView: () => ({
        showFilteredImage: (image, text = "", ms = 1500) => jplacer.call("showPipelineImage", { ms: Math.trunc(ms), text: String(text) }),
    }),
};

const Utils2D = {
    calculateBoardPlacementLocation(board, location = new Location()) {
        const l = location.convertToUnits(LengthUnit.Millimeters);
        const r = jplacer.call("boardPlacementLocation",
                               { board: board.index, location: { x: l.x, y: l.y, z: l.z, rotation: l.rotation } });
        return new Location(LengthUnit.Millimeters, r.x, r.y, r.z, r.rotation);
    },
};
const VisionUtils = { readQrCode: (camera) => jplacer.call("readQrCode", { camera: camera.id }) };

const machine = new Machine();
const config = new Configuration();
const scripting = new Scripting();
const gui = new Gui();

// The Java packages OpenPnP's scripts use, as far as jplacer has them.
const UiUtils = {
    submitUiMachineTask(task) {
        try {
            return task();
        } catch (e) {
            jplacer.call("dialog", { title: "Error", text: String(e && e.message ? e.message : e) });
            throw e;
        }
    },
};
UiUtils["submitUiMachineTask(Thrunnable)"] = UiUtils.submitUiMachineTask;
const engines = () => [
    ["node", process.versions.node, "javascript", ["js"]], ["python3", "", "python", ["py"]], ["sh", "", "shell", ["sh"]],
].map(([engine, version, language, extensions]) => ({
    getEngineName: () => engine, getEngineVersion: () => version, getLanguageName: () => language,
    getLanguageVersion: () => version, getExtensions: () => "[" + extensions.join(", ") + "]",
}));
const org = { openpnp: { model: { Location, LengthUnit, Part }, util: { UiUtils, Utils2D, VisionUtils, OpenCvUtils },
                         vision: { pipeline: { CvPipeline } } } };
const javax = {
    swing: { JOptionPane: { showMessageDialog: (parent, message, title) =>
        jplacer.call("dialog", { title: title ? String(title) : "Message", text: String(message) }) } },
    script: { ScriptEngineManager: class { getEngineFactories() { return engines(); } } },
};

// Nashorn's own syntax, as node takes it: `for each (x in list)` is `for (x of list)`.
function nashorn(source) {
    return source.replace(/for\s+each\s*\(\s*((?:var|let|const)\s+)?([A-Za-z_$][\w$]*)\s+in\s+/g, "for ($1$2 of ");
}

// The script at `file` run as OpenPnP runs it.
function run(file) {
    const context = vm.createContext({
        machine, config, scripting, gui, org, javax, Packages: { org, javax },
        print: (...values) => console.log(values.join(" ")),
        // A package's names, or a class by its own (JavaImporter(org.openpnp.vision.pipeline.CvPipeline)).
        JavaImporter: function (...packages) {
            const names = {};
            for (const p of packages) {
                if (typeof p === "function") names[p.name] = p;
                else if (p) Object.assign(names, p);
            }
            return names;
        },
        require, console, process, module: { exports: {} }, __filename: file, __dirname: require("path").dirname(file),
    });
    context.exports = context.module.exports;
    for (const [name, value] of Object.entries(jplacer.globals)) context[name] = value;   // what it is run for
    context.load = (path) => vm.runInContext(nashorn(fs.readFileSync(String(path), "utf8")), context, { filename: String(path) });
    context.load(file);
}

module.exports = { run, nashorn, Location, LengthUnit, machine, config, scripting };
)JS";

// The Java packages OpenPnP's Python scripts import, as far as jplacer has them:
// each (a path under the scripts) and what it holds.
const std::vector<std::pair<const char*, const char*>> kOpenPnpPackages {
    { "org/__init__.py", "" },
    { "org/openpnp/__init__.py", "" },
    { "org/openpnp/model/__init__.py", "from jplacer_openpnp import Location, LengthUnit, Part\n" },
    { "org/openpnp/util/__init__.py", "from jplacer_openpnp import Utils2D, VisionUtils, OpenCvUtils\n" },
    { "org/openpnp/vision/__init__.py", "" },
    { "org/openpnp/vision/pipeline/__init__.py", "from jplacer_openpnp import CvPipeline\n" },
    { "org/openpnp/util/UiUtils.py",
      "# OpenPnP's UiUtils: a machine task run now; an error it raises shown, as OpenPnP shows it.\n"
      "import jplacer\n\n"
      "def submitUiMachineTask(task):\n"
      "    try:\n"
      "        return task()\n"
      "    except Exception as e:\n"
      "        jplacer.call('dialog', title='Error', text=str(e))\n"
      "        raise\n" },
    { "javax/__init__.py", "" },
    { "javax/swing/__init__.py", "" },
    { "javax/swing/JOptionPane.py",
      "# Java's JOptionPane: a message shown in jplacer's dialog.\n"
      "import jplacer\n\n"
      "def showMessageDialog(parent, message, title='Message', kind=None):\n"
      "    jplacer.call('dialog', title=str(title), text=str(message))\n" },
    { "javax/script/__init__.py",
      "# Java's ScriptEngineManager: the languages jplacer runs scripts in.\n"
      "import platform\n\n"
      "class _Factory(object):\n"
      "    def __init__(self, engine, version, language, extensions):\n"
      "        self._e = (engine, version, language, extensions)\n"
      "    def getEngineName(self): return self._e[0]\n"
      "    def getEngineVersion(self): return self._e[1]\n"
      "    def getLanguageName(self): return self._e[2]\n"
      "    def getLanguageVersion(self): return self._e[1]\n"
      "    def getExtensions(self): return self._e[3]\n\n"
      "class ScriptEngineManager(object):\n"
      "    def getEngineFactories(self):\n"
      "        return [_Factory('python3', platform.python_version(), 'python', ['py']),\n"
      "                _Factory('node', '', 'javascript', ['js']), _Factory('sh', '', 'shell', ['sh'])]\n" },
};

// The helper modules' folder under the scripts; and the first line of each as
// an older jplacer wrote it beside the scripts.
constexpr const char* kHelpersDir = ".jplacer";
constexpr const char* kOldModuleHeaderPython = "# jplacer's machine, for the scripts jplacer runs (as OpenPnP's scripts have `machine`).";
constexpr const char* kOldModuleHeaderNode = "// jplacer's machine, for the scripts jplacer runs (as OpenPnP's scripts have `machine`).";

// A JavaScript script started as OpenPnP's Nashorn runs it.
constexpr const char* kNodeStart = "require('jplacer_openpnp').run(process.argv[1])";

// A Python script started with OpenPnP's globals.
constexpr const char* kPythonStart =
    "import sys, runpy, jplacer_openpnp; jplacer_openpnp.install(); sys.argv = sys.argv[1:]; "
    "runpy.run_path(sys.argv[0], run_name='__main__')";

// OpenPnP's pooled script engines: an interpreter that stays, running each
// script it is sent (JPScriptProcess, served) as kPythonStart and kNodeStart
// run one, with that run's globals; what a script leaves behind (a module's
// state, the working folder) stays, as OpenPnP warns.
constexpr const char* kPythonServe = R"PY(
import sys, os, json, runpy, traceback
import jplacer, jplacer_openpnp
_runs = os.fdopen(5, "r")
_ends = os.fdopen(6, "w")
for _line in _runs:
    _run = json.loads(_line)
    jplacer.globals = _run["globals"]
    jplacer.event = _run["event"]
    os.environ["JPLACER_GLOBALS"] = json.dumps(_run["globals"])
    os.environ["JPLACER_EVENT"] = _run["event"]
    _code = 0
    try:
        jplacer_openpnp.install()
        os.chdir(os.path.dirname(_run["path"]))
        sys.argv = [_run["path"]]
        runpy.run_path(_run["path"], run_name="__main__")
    except SystemExit as e:
        if isinstance(e.code, int):
            _code = e.code
        elif e.code is not None:
            print(e.code, file=sys.stderr)
            _code = 1
    except BaseException:
        traceback.print_exc()
        _code = 1
    sys.stdout.flush()
    sys.stderr.flush()
    _ends.write(json.dumps({"exit": _code}) + "\n")
    _ends.flush()
)PY";

constexpr const char* kNodeServe = R"JS(
const fs = require("fs");
const path = require("path");
const jplacer = require("jplacer");
const openpnp = require("jplacer_openpnp");
class Exit { constructor(code) { this.code = code; } }
const leave = process.exit.bind(process);
process.exit = (code) => { throw new Exit(code === undefined ? 0 : code); };
const buf = Buffer.alloc(65536);
let pending = "";
for (;;) {
    while (!pending.includes("\n")) {
        let n = 0;
        try { n = fs.readSync(5, buf, 0, buf.length, null); } catch (e) { if (e.code === "EAGAIN") continue; throw e; }
        if (n <= 0) leave(0);
        pending += buf.toString("utf8", 0, n);
    }
    const nl = pending.indexOf("\n");
    const run = JSON.parse(pending.slice(0, nl));
    pending = pending.slice(nl + 1);
    jplacer.globals = run.globals;
    jplacer.event = run.event;
    process.env.JPLACER_GLOBALS = JSON.stringify(run.globals);
    process.env.JPLACER_EVENT = run.event;
    let code = 0;
    try {
        process.chdir(path.dirname(run.path));
        openpnp.run(run.path);
    } catch (e) {
        if (e instanceof Exit) code = e.code;
        else { console.error(e && e.stack ? e.stack : String(e)); code = 1; }
    }
    fs.writeSync(6, JSON.stringify({ exit: code }) + "\n");
}
)JS";

// `text` written to `path` unless it is there already.
void keep(const fs::path& path, const std::string& text) {
    std::ifstream in(path, std::ios::binary);
    const std::string was((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (in.is_open() && was == text) return;
    std::ofstream(path, std::ios::binary | std::ios::trunc) << text;
}

} // namespace

JPScripting::JPScripting(std::string scriptsDirectory) : m_directory(std::move(scriptsDirectory)) {
    std::error_code ec;
    fs::create_directories(eventsDirectory(), ec);
    // The helper modules, out of the Scripts menu (a folder with .ignore, as OpenPnP leaves out).
    const fs::path helpers = helpersDirectory();
    fs::create_directories(helpers, ec);
    keep(helpers / ".ignore", "");
    keep(helpers / "jplacer.py", kPythonModule);
    keep(helpers / "jplacer.js", kNodeModule);
    keep(helpers / "jplacer_openpnp.py", kOpenPnpModel);
    keep(helpers / "jplacer_openpnp.js", kOpenPnpModelJs);
    for (const auto& [file, text] : kOpenPnpPackages) {
        fs::create_directories((helpers / file).parent_path(), ec);
        keep(helpers / file, text);
    }
    // Where an older jplacer wrote them, beside the scripts: taken away when they are its own.
    for (const auto& [file, text] : { std::pair { "jplacer.py", kOldModuleHeaderPython }, std::pair { "jplacer.js", kOldModuleHeaderNode } }) {
        std::ifstream in(fs::path(m_directory) / file);
        std::string first;
        if (in && std::getline(in, first) && first == text) {
            in.close();
            fs::remove(fs::path(m_directory) / file, ec);
        }
    }
}

std::string JPScripting::eventsDirectory() const { return (fs::path(m_directory) / "Events").string(); }

fs::path JPScripting::helpersDirectory() const { return fs::path(m_directory) / kHelpersDir; }

const std::vector<std::pair<std::string, std::string>>& JPScripting::interpreters() {
    static const std::vector<std::pair<std::string, std::string>> k { { ".py", "python3" }, { ".js", "node" }, { ".sh", "sh" } };
    return k;
}

bool JPScripting::runnable(const std::string& path) {
    const std::string ext = fs::path(path).extension().string();
    return std::any_of(interpreters().begin(), interpreters().end(), [&ext](const auto& i) { return i.first == ext; });
}

void JPScripting::refresh() {
    {
        std::lock_guard lk(m_mutex);
        m_eventsWithout.clear();
    }
    if (onPoolChanged) onPoolChanged();
}

bool JPScripting::execute(const std::string& path, const JJson& globals, std::string& why, const std::string& event) {
    const std::string ext = fs::path(path).extension().string();
    std::string program;
    for (const auto& [e, p] : interpreters())
        if (e == ext) program = p;
    if (program.empty()) {
        why = path + ": no way to run a " + ext + " script";
        return false;
    }
    const std::string name = fs::path(path).filename().string();
    // Its environment: ours, and where the machine is asked and the helper modules are; run once,
    // what it is run for (served, each run says).
    std::vector<std::string> env;
    for (char** e = environ; *e; ++e) env.emplace_back(*e);
    env.push_back("JPLACER_SCRIPTS=" + m_directory);
    env.push_back("JPLACER_API=" + std::to_string(kRequestFd) + "," + std::to_string(kAnswerFd));
    {
        const char* py = std::getenv("PYTHONPATH");
        const char* node = std::getenv("NODE_PATH");
        const std::string helpers = helpersDirectory().string();
        env.push_back("PYTHONPATH=" + helpers + (py && *py ? ":" + std::string(py) : std::string()));
        env.push_back("NODE_PATH=" + helpers + (node && *node ? ":" + std::string(node) : std::string()));
    }
    // OpenPnP's scripting.on and scripting.execute, asked by the script: answered here (each on an interpreter of its
    // own: this one is busy), anything else asked of the machine.
    const std::function<JJson(const JJson&)> served = [this](const JJson& request) {
        const std::string call = request["call"].str();
        if (call != "scriptingOn" && call != "scriptingExecute") {
            if (api) return api(request);
            JJson none = JJson::object();
            none["error"] = std::string("no machine to ask");
            return none;
        }
        std::string w;
        bool ok = false;
        if (call == "scriptingOn") {
            ok = on(request["event"].str(), request["globals"], w);
        } else {
            fs::path script = request["path"].str();
            if (script.is_relative()) script = fs::path(m_directory) / script;
            ok = execute(script.string(), request["globals"], w);
        }
        JJson answer = JJson::object();
        if (ok) answer["result"] = JJson();
        else answer["error"] = w;
        return answer;
    };
    JPScriptProcess::Result r;
    const bool pooled = m_pooling && (ext == ".py" || ext == ".js");
    if (pooled) {
        // OpenPnP's pooled script engines: an interpreter left from before, or a new one, serving this run.
        std::unique_ptr<JPScriptProcess> engine;
        {
            std::lock_guard lk(m_poolMutex);
            auto& idle = m_idle[ext];
            if (!idle.empty()) {
                engine = std::move(idle.back());
                idle.pop_back();
            }
        }
        if (!engine) {
            const std::vector<std::string> argv = ext == ".py" ? std::vector<std::string> { program, "-c", kPythonServe }
                                                               : std::vector<std::string> { program, "-e", kNodeServe };
            engine = JPScriptProcess::spawn(argv, env, m_directory, true, why);
            if (!engine) {
                why = name + ": " + why;
                return false;
            }
        }
        JJson run = JJson::object();
        run["path"] = fs::absolute(path).string();
        run["globals"] = globals;
        run["event"] = event;
        r = engine->run(name, run, served, kTimeoutMs);
        if (engine->serving() && m_pooling) {
            {
                std::lock_guard lk(m_poolMutex);
                m_idle[ext].push_back(std::move(engine));
            }
            if (onPoolChanged) onPoolChanged();
        }
    } else {
        env.push_back("JPLACER_GLOBALS=" + globals.dump());
        env.push_back("JPLACER_EVENT=" + event);
        // Python and JavaScript: started with OpenPnP's globals (machine, config, scripting, gui).
        const std::vector<std::string> argv = ext == ".py"   ? std::vector<std::string> { program, "-c", kPythonStart, path }
                                            : ext == ".js"   ? std::vector<std::string> { program, "-e", kNodeStart, path }
                                                             : std::vector<std::string> { program, path };
        const auto once = JPScriptProcess::spawn(argv, env, fs::path(path).parent_path().string(), false, why);
        if (!once) {
            why = name + ": " + why;
            return false;
        }
        r = once->run(name, JJson(), served, kTimeoutMs);
    }
    if (r.end == JPScriptProcess::Result::End::TimedOut) {
        why = name + " did not finish within " + std::to_string(kTimeoutMs / 1000) + " s";
        return false;
    }
    if (r.end == JPScriptProcess::Result::End::Lost) {
        why = name + " failed: its script engine ended" + (r.last.empty() ? "" : ": " + r.last);
        return false;
    }
    if (r.code != 0) {
        why = name + " failed" + (r.code > 0 ? " (exit " + std::to_string(r.code) + ")" : "") + (r.last.empty() ? "" : ": " + r.last);
        return false;
    }
    return true;
}

void JPScripting::setPooling(bool on) {
    m_pooling = on;
    if (on) return;
    {
        std::lock_guard lk(m_poolMutex);
        m_idle.clear();   // each ends as its runs are closed
    }
    if (onPoolChanged) onPoolChanged();
}

int JPScripting::clearPool() {
    int ended = 0;
    {
        std::lock_guard lk(m_mutex);
        m_eventsWithout.clear();
    }
    {
        std::lock_guard lk(m_poolMutex);
        for (const auto& [ext, idle] : m_idle) ended += int(idle.size());
        m_idle.clear();
    }
    JLOGC(JPlacerLog::kApp, JLogLevel::Info) << (ended ? std::to_string(ended) + " scripting engine(s) cleared from the pool"
                                                       : std::string("No scripting engines in pool, nothing to do"));
    if (onPoolChanged) onPoolChanged();
    return ended;
}

bool JPScripting::canClearPool() {
    {
        std::lock_guard lk(m_poolMutex);
        for (const auto& [ext, idle] : m_idle)
            if (!idle.empty()) return true;
    }
    std::lock_guard lk(m_mutex);
    return !m_eventsWithout.empty();
}

JPScripting::~JPScripting() {
    std::lock_guard lk(m_poolMutex);
    m_idle.clear();
}

bool JPScripting::on(const std::string& event, const JJson& globals, std::string& why) {
    {
        std::lock_guard lk(m_mutex);
        if (m_eventsWithout.count(event)) return true;
    }
    std::vector<fs::path> scripts;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(eventsDirectory(), ec)) {
        if (!e.is_regular_file() || !runnable(e.path().string())) continue;
        const std::string base = e.path().stem().string();
        if (base == event || base.rfind(event + ".", 0) == 0) scripts.push_back(e.path());
    }
    if (scripts.empty()) {
        {
            std::lock_guard lk(m_mutex);
            m_eventsWithout.insert(event);
        }
        if (onPoolChanged) onPoolChanged();
        return true;
    }
    std::sort(scripts.begin(), scripts.end(), [](const fs::path& a, const fs::path& b) { return a.filename() < b.filename(); });
    for (const fs::path& s : scripts) {
        JLOGC(JPlacerLog::kApp, JLogLevel::Info) << "event " << event << ": " << s.filename().string();
        if (!execute(s.string(), globals, why, event)) return false;
    }
    return true;
}

} // inline namespace jf
