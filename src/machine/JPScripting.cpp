// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPScripting.h"

#include "common/JPlacerLog.h"

#include <j/core/Log.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <thread>

#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

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
    os.write(_request, (json.dumps(arguments) + "\n").encode())
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

    def __str__(self):
        return "Scripting"


machine = Machine()
config = Configuration()
scripting = Scripting()


def install():
    """OpenPnP's globals for the script about to run."""
    builtins.machine = machine
    builtins.config = config
    builtins.scripting = scripting
    builtins.gui = None
    for name, value in jplacer.globals.items():   # what the script is run for (an event's globals)
        setattr(builtins, name, value)
)PY";

// The Java packages OpenPnP's Python scripts import, as far as jplacer has them:
// each (a path under the scripts) and what it holds.
const std::vector<std::pair<const char*, const char*>> kOpenPnpPackages {
    { "org/__init__.py", "" },
    { "org/openpnp/__init__.py", "" },
    { "org/openpnp/model/__init__.py", "from jplacer_openpnp import Location, LengthUnit, Part\n" },
    { "org/openpnp/util/__init__.py", "" },
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

// A Python script started with OpenPnP's globals.
constexpr const char* kPythonStart =
    "import sys, runpy, jplacer_openpnp; jplacer_openpnp.install(); sys.argv = sys.argv[1:]; "
    "runpy.run_path(sys.argv[0], run_name='__main__')";

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
    std::lock_guard lk(m_mutex);
    m_eventsWithout.clear();
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
    // Its output; and the machine's API (JPScripting::api): its requests on fd 3, the answers on fd 4.
    int out[2], req[2], rep[2];
    if (::pipe2(out, O_CLOEXEC) != 0) {
        why = path + ": cannot be run";
        return false;
    }
    if (::pipe2(req, O_CLOEXEC) != 0 || ::pipe2(rep, O_CLOEXEC) != 0) {
        ::close(out[0]);
        ::close(out[1]);
        why = path + ": cannot be run";
        return false;
    }
    // Its environment: ours, and what it is run for.
    std::vector<std::string> env;
    for (char** e = environ; *e; ++e) env.emplace_back(*e);
    env.push_back("JPLACER_GLOBALS=" + globals.dump());
    env.push_back("JPLACER_EVENT=" + event);
    env.push_back("JPLACER_SCRIPTS=" + m_directory);
    env.push_back("JPLACER_API=" + std::to_string(kRequestFd) + "," + std::to_string(kAnswerFd));
    // The helper modules (jplacer.py, jplacer.js) beside the scripts, found wherever the script is.
    {
        const char* py = std::getenv("PYTHONPATH");
        const char* node = std::getenv("NODE_PATH");
        const std::string helpers = helpersDirectory().string();
        env.push_back("PYTHONPATH=" + helpers + (py && *py ? ":" + std::string(py) : std::string()));
        env.push_back("NODE_PATH=" + helpers + (node && *node ? ":" + std::string(node) : std::string()));
    }
    std::vector<char*> envp;
    for (std::string& e : env) envp.push_back(e.data());
    envp.push_back(nullptr);
    std::string prog = program, file = path, dashC = "-c", start = kPythonStart;
    // Python: started with OpenPnP's globals (machine, config, scripting, gui).
    char* plain[] = { prog.data(), file.data(), nullptr };
    char* python[] = { prog.data(), dashC.data(), start.data(), file.data(), nullptr };
    char** argv = ext == ".py" ? python : plain;
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions, out[1], STDOUT_FILENO);
    posix_spawn_file_actions_adddup2(&actions, out[1], STDERR_FILENO);
    posix_spawn_file_actions_adddup2(&actions, req[1], kRequestFd);
    posix_spawn_file_actions_adddup2(&actions, rep[0], kAnswerFd);
    posix_spawn_file_actions_addchdir_np(&actions, fs::path(path).parent_path().c_str());
    pid_t pid = 0;
    const int failed = posix_spawnp(&pid, prog.c_str(), &actions, nullptr, argv, envp.data());
    posix_spawn_file_actions_destroy(&actions);
    ::close(out[1]);
    ::close(req[1]);
    ::close(rep[0]);
    auto closeApi = [&req, &rep] {
        ::close(req[0]);
        ::close(rep[1]);
    };
    if (failed) {
        ::close(out[0]);
        closeApi();
        why = fs::path(path).filename().string() + ": " + program + " is not installed";
        return false;
    }
    // What it prints, line by line to the log, until it ends or its time is up; what it
    // asks of the machine, a line of JSON each, answered a line each.
    std::string text, last, asked;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(kTimeoutMs);
    bool timedOut = false, asking = true;
    for (;;) {
        const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()).count();
        if (left <= 0) {
            timedOut = true;
            break;
        }
        pollfd p[2] = { { out[0], POLLIN, 0 }, { req[0], short(asking ? POLLIN : 0), 0 } };
        if (::poll(p, 2, int(left)) <= 0) continue;
        if (asking && (p[1].revents & (POLLIN | POLLHUP))) {
            char buf[4096];
            const ssize_t n = ::read(req[0], buf, sizeof buf);
            if (n <= 0) asking = false;   // closed its end
            else asked.append(buf, size_t(n));
            for (size_t nl; (nl = asked.find('\n')) != std::string::npos;) {
                const std::string line = asked.substr(0, nl);
                asked.erase(0, nl + 1);
                JJson answer = JJson::object();
                const JJson request = JJson::parse(line);
                if (!request.isObject()) answer["error"] = std::string("not a request: ") + line;
                else if (!api) answer["error"] = std::string("no machine to ask");
                else answer = api(request);
                const std::string reply = answer.dump() + "\n";
                if (::write(rep[1], reply.data(), reply.size()) < 0) asking = false;
            }
        }
        if (!(p[0].revents & (POLLIN | POLLHUP))) continue;
        char buf[4096];
        const ssize_t n = ::read(out[0], buf, sizeof buf);
        if (n <= 0) break;
        text.append(buf, size_t(n));
        for (size_t nl; (nl = text.find('\n')) != std::string::npos;) {
            last = text.substr(0, nl);
            JLOGC(JPlacerLog::kApp, JLogLevel::Info) << fs::path(path).filename().string() << ": " << last;
            text.erase(0, nl + 1);
        }
    }
    ::close(out[0]);
    closeApi();
    if (timedOut) ::kill(pid, SIGKILL);
    int status = 0;
    ::waitpid(pid, &status, 0);
    if (!text.empty()) {
        last = text;
        JLOGC(JPlacerLog::kApp, JLogLevel::Info) << fs::path(path).filename().string() << ": " << text;
    }
    if (timedOut) {
        why = fs::path(path).filename().string() + " did not finish within " + std::to_string(kTimeoutMs / 1000) + " s";
        return false;
    }
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        why = fs::path(path).filename().string() + " failed" + (WIFEXITED(status) ? " (exit " + std::to_string(WEXITSTATUS(status)) + ")" : "")
            + (last.empty() ? "" : ": " + last);
        return false;
    }
    return true;
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
        std::lock_guard lk(m_mutex);
        m_eventsWithout.insert(event);
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
