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

// `text` written to `path` unless it is there already.
void keep(const fs::path& path, const std::string& text) {
    std::ifstream in(path, std::ios::binary);
    const std::string was((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (was == text) return;
    std::ofstream(path, std::ios::binary | std::ios::trunc) << text;
}

} // namespace

JPScripting::JPScripting(std::string scriptsDirectory) : m_directory(std::move(scriptsDirectory)) {
    std::error_code ec;
    fs::create_directories(eventsDirectory(), ec);
    keep(fs::path(m_directory) / "jplacer.py", kPythonModule);
    keep(fs::path(m_directory) / "jplacer.js", kNodeModule);
}

std::string JPScripting::eventsDirectory() const { return (fs::path(m_directory) / "Events").string(); }

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
        env.push_back("PYTHONPATH=" + m_directory + (py && *py ? ":" + std::string(py) : std::string()));
        env.push_back("NODE_PATH=" + m_directory + (node && *node ? ":" + std::string(node) : std::string()));
    }
    std::vector<char*> envp;
    for (std::string& e : env) envp.push_back(e.data());
    envp.push_back(nullptr);
    std::string prog = program, file = path;
    char* argv[] = { prog.data(), file.data(), nullptr };
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
