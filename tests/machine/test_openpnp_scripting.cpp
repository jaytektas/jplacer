// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// OpenPnP's ScriptingTest, for the script engines this computer has (as OpenPnP's, which tests the ones present):
// Python and JavaScript. An event's script of each kind run; an event's scripts named after it with more to the name
// run in name order, one only starting with its name not; a script that throws fails, its pooled engine kept for the
// next; with pooling, an engine of each kind kept, and cleared; and 20 threads at once each running a script that
// runs the event from a thread of its own (its own engine, as the running one is busy). OpenPnP's test scripts put
// their results in a map shared with the test; jplacer's scripts are programs of their own, so these write files.
// Tests check with assert(); a Release build must not compile it away.
#undef NDEBUG
#include <cassert>

#include "machine/JPScripting.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <unistd.h>
#include <vector>

using namespace jf;
namespace fs = std::filesystem;

namespace {

void write(const fs::path& p, const std::string& text) {
    fs::create_directories(p.parent_path());
    std::ofstream(p) << text;
}

std::string read(const fs::path& p) {
    std::ifstream in(p);
    std::stringstream s;
    s << in.rdbuf();
    return s.str();
}

bool have(const char* program) { return std::system((std::string("command -v ") + program + " >/dev/null 2>&1").c_str()) == 0; }

} // namespace

int main() {
    const fs::path dir = fs::temp_directory_path() / ("jplacer-openpnp-scripting-" + std::to_string(::getpid()));
    fs::remove_all(dir);
    const fs::path results = dir / "results";
    fs::create_directories(results);
    JPScripting scripting((dir / "scripts").string());
    const fs::path scripts = dir / "scripts", events = scripting.eventsDirectory();

    // The engines for which there are test scripts, those this computer has.
    std::vector<std::string> extensions;
    if (have("python3")) extensions.push_back("py");
    if (have("node")) extensions.push_back("js");
    if (extensions.size() < 2) std::fprintf(stderr, "Warning: script engines missing here cannot be tested\n");
    if (extensions.empty()) return 0;

    // testEvent.<ext>: its result, and with threadedTest one for its thread.
    write(events / "testEvent.py", "import os\n"
                                   "open(os.path.join(resultsDir, 'py'), 'w').write('ok')\n"
                                   "if threadedTest:\n"
                                   "    open(os.path.join(resultsDir, 'py' + str(threadId)), 'w').write('ok')\n");
    write(events / "testEvent.js", "const fs = require('fs');\n"
                                   "fs.writeFileSync(resultsDir + '/js', 'ok');\n"
                                   "if (threadedTest) fs.writeFileSync(resultsDir + '/js' + threadId, 'ok');\n");
    const std::string ext0 = extensions.front();
    // The other scripts, in the first engine there is.
    if (ext0 == "py") {
        write(events / "testFilename.OtherTextCanBeHere.py", "import os\nopen(os.path.join(resultsDir, 'other'), 'w').write('first')\n");
        write(events / "testFilename.XYZ.py", "import os\nopen(os.path.join(resultsDir, 'other'), 'a').write('+xyz')\n");
        write(events / "testFilenameButNotThis.py", "import os\nopen(os.path.join(resultsDir, 'other'), 'a').write('+wrong')\n");
        write(scripts / "throwException.py", "raise Exception('test exception')\n");
        write(scripts / "callScriptFromScript.py", "import os, threading\n"
                                                   "open(os.path.join(resultsDir, 'base' + str(threadId)), 'w').write('ok')\n"
                                                   "thread = threading.Thread(target=lambda: scripting.on('testEvent', testGlobals))\n"
                                                   "thread.start()\n"
                                                   "thread.join()\n");
    } else {
        write(events / "testFilename.OtherTextCanBeHere.js", "require('fs').writeFileSync(resultsDir + '/other', 'first');\n");
        write(events / "testFilename.XYZ.js", "require('fs').appendFileSync(resultsDir + '/other', '+xyz');\n");
        write(events / "testFilenameButNotThis.js", "require('fs').appendFileSync(resultsDir + '/other', '+wrong');\n");
        write(scripts / "throwException.js", "throw new Error('test exception');\n");
        write(scripts / "callScriptFromScript.js", "require('fs').writeFileSync(resultsDir + '/base' + threadId, 'ok');\n"
                                                   "scripting.on('testEvent', testGlobals);\n");
    }
    JJson globals = JJson::object();
    globals["resultsDir"] = results.string();
    globals["threadedTest"] = false;
    std::string why;

    // Test 1: every engine's script for the event run; no engines kept without pooling.
    scripting.setPooling(false);
    assert(scripting.on("testEvent", globals, why));
    for (const std::string& e : extensions) assert(read(results / e) == "ok");
    assert(scripting.clearPool() == 0);

    // Test 1b: an event's scripts named after it with more to the name, in name order; not one only starting with it.
    assert(scripting.on("testFilename", globals, why));
    assert(read(results / "other") == "first+xyz");

    // Test 2: a script that throws fails; its pooled engine kept for the next, the same one used again.
    scripting.setPooling(true);
    const std::string thrower = (scripts / ("throwException." + ext0)).string();
    assert(!scripting.execute(thrower, globals, why) && why.find("test exception") != std::string::npos);
    assert(!scripting.execute(thrower, globals, why));
    assert(scripting.clearPool() == 1);

    // Test 3: an engine of each kind kept once the event has run.
    assert(scripting.on("testEvent", globals, why));
    // Test 4: cleared, none left.
    assert(scripting.clearPool() == int(extensions.size()));
    assert(scripting.clearPool() == 0);

    // Test 5: threads at once, each a script running the event from a thread of its own.
    globals["threadedTest"] = true;
    const int threads = 20;
    std::vector<std::thread> running;
    for (int id = 0; id < threads; ++id)
        running.emplace_back([&, id] {
            JJson g = globals;
            g["threadId"] = id;
            g["testGlobals"] = g;
            std::string w;
            scripting.execute((scripts / ("callScriptFromScript." + ext0)).string(), g, w);
        });
    for (std::thread& t : running) t.join();
    for (int id = 0; id < threads; ++id) {
        for (const std::string& e : extensions) assert(read(results / (e + std::to_string(id))) == "ok");
        assert(read(results / ("base" + std::to_string(id))) == "ok");
    }
    std::fprintf(stderr, "All %d threads returned the expected results\n", threads);
    scripting.clearPool();
    fs::remove_all(dir);
    return 0;
}
