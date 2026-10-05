// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// OpenPnP's scripting, as jplacer runs it: a script run as a program, with
// what it is run for in JPLACER_GLOBALS and the event in JPLACER_EVENT; an
// event's scripts (named the event, or the event and a dot) run in name
// order; one failing stops it, saying why; a folder with none is quiet.
// Tests check with assert(); a Release build must not compile it away.
#undef NDEBUG
#include <cassert>

#include "machine/JPScripting.h"

#include <filesystem>
#include <fstream>
#include <string>

using namespace jf;
namespace fs = std::filesystem;

int main() {
    const fs::path dir = fs::temp_directory_path() / "jplacer-test-scripts";
    fs::remove_all(dir);
    JPScripting scripting(dir.string());
    assert(fs::is_directory(scripting.eventsDirectory()));
    const fs::path log = dir / "log.txt";
    auto write = [](const fs::path& p, const std::string& text) { std::ofstream(p) << text; };
    // Two for Job.Starting, one for another event, one not a script.
    write(fs::path(scripting.eventsDirectory()) / "Job.Starting.sh",
          "echo \"first $JPLACER_EVENT $JPLACER_GLOBALS\" >> " + log.string() + "\n");
    write(fs::path(scripting.eventsDirectory()) / "Job.Starting.2.sh", "echo second >> " + log.string() + "\n");
    write(fs::path(scripting.eventsDirectory()) / "Job.StartingSoon.sh", "echo wrong >> " + log.string() + "\n");
    write(fs::path(scripting.eventsDirectory()) / "Job.Starting.txt", "not run\n");
    JJson globals = JJson::object();
    globals["job"] = "board.job.xml";
    std::string why;
    assert(scripting.on("Job.Starting", globals, why));
    {
        std::ifstream in(log);
        std::string a, b, c;
        std::getline(in, a);
        std::getline(in, b);
        // By file name, as OpenPnP sorts them: "Job.Starting.2.sh" before "Job.Starting.sh".
        assert(a == "second" && b == "first Job.Starting {\"job\":\"board.job.xml\"}" && !std::getline(in, c));
    }
    // No scripts for it: nothing run, nothing wrong.
    assert(scripting.on("Nozzle.BeforePick", globals, why));
    // A failing one stops the event, saying so.
    write(fs::path(scripting.eventsDirectory()) / "Job.Error.sh", "echo broken; exit 3\n");
    assert(!scripting.on("Job.Error", globals, why) && why.find("exit 3") != std::string::npos && why.find("broken") != std::string::npos);
    // A script of its own, from the menu; one of a kind not run.
    write(dir / "hello.sh", "exit 0\n");
    assert(scripting.execute((dir / "hello.sh").string(), JJson::object(), why));
    assert(!JPScripting::runnable((dir / "notes.txt").string()) && JPScripting::runnable((dir / "a.py").string()));
    fs::remove_all(dir);
    return 0;
}
