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

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

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
    // Asking the machine (OpenPnP's scripts have `machine`): the helper module kept beside the scripts, each
    // request answered, an error raised in the script; with nothing to ask, said so.
    assert(fs::exists(scripting.helpersDirectory() / "jplacer.py") && fs::exists(scripting.helpersDirectory() / "jplacer.js"));
    assert(fs::exists(scripting.helpersDirectory() / ".ignore") && !fs::exists(dir / "jplacer.py"));
    std::vector<std::string> asked;
    scripting.api = [&asked](const JJson& request) {
        asked.push_back(request["call"].str());
        JJson answer = JJson::object();
        if (request["call"].str() == "location") {
            answer["result"]["x"] = 12.5;
            answer["result"]["tool"] = request["tool"].str();
        } else answer["error"] = std::string("not today");
        return answer;
    };
    if (std::system("python3 -c '' >/dev/null 2>&1") == 0) {
        write(dir / "ask.py", "import jplacer\n"
                              "at = jplacer.location('N1')\n"
                              "assert at['x'] == 12.5 and at['tool'] == 'N1', at\n"
                              "try:\n"
                              "    jplacer.home()\n"
                              "    raise SystemExit(5)\n"
                              "except jplacer.Error as e:\n"
                              "    assert str(e) == 'not today'\n"
                              "print('asked', jplacer.event)\n");
        const bool ok = scripting.execute((dir / "ask.py").string(), JJson::object(), why, "Test.Event");
        if (!ok) std::fprintf(stderr, "why: %s\n", why.c_str());
        assert(ok && asked == (std::vector<std::string> { "location", "home" }));
        // OpenPnP's own Python scripts, as OpenPnP's examples are written: its
        // globals and Java imports, a nozzle moved by Location, a feeder reset.
        JJson moved = JJson::array();
        int feedCountSet = -1;
        std::string dialog;
        bool boardEnabled = true;
        std::string pipelineXml, pipelineCamera;
        int shownMs = 0;
        scripting.api = [&](const JJson& request) {
            JJson answer = JJson::object();
            const std::string call = request["call"].str();
            if (call == "machine") {
                JJson nozzle = JJson::object();
                nozzle["id"] = std::string("N1");
                nozzle["name"] = std::string("N1");
                nozzle["tip"] = std::string("T1");
                nozzle["part"] = std::string();
                JJson head = JJson::object();
                head["id"] = std::string("H1");
                head["name"] = std::string("H1");
                head["nozzles"] = JJson::array();
                head["nozzles"].push(nozzle);
                head["cameras"] = JJson::array();
                JJson camera = JJson::object();
                camera["id"] = std::string("C1");
                camera["name"] = std::string("Top");
                camera["looking"] = std::string("Down");
                head["cameras"].push(camera);
                head["actuators"] = JJson::array();
                JJson feeder = JJson::object();
                feeder["id"] = std::string("F1");
                feeder["name"] = std::string("Strip");
                feeder["part"] = std::string("R1");
                feeder["enabled"] = true;
                feeder["feedCount"] = 7;
                answer["result"]["name"] = std::string("Test");
                answer["result"]["heads"] = JJson::array();
                answer["result"]["heads"].push(head);
                answer["result"]["cameras"] = JJson::array();
                answer["result"]["actuators"] = JJson::array();
                answer["result"]["feeders"] = JJson::array();
                answer["result"]["feeders"].push(feeder);
                answer["result"]["parts"] = JJson::array();
            } else if (call == "location") {
                answer["result"]["x"] = 1.0;
                answer["result"]["y"] = 2.0;
                answer["result"]["z"] = 0.0;
                answer["result"]["rotation"] = 0.0;
            } else if (call == "moveTo") {
                moved.push(request["x"]);
            } else if (call == "setFeedCount") {
                feedCountSet = int(request["count"].number());
            } else if (call == "dialog") {
                dialog = request["text"].str();
            } else if (call == "job") {
                JJson board = JJson::object();
                board["id"] = std::string("B1");
                board["name"] = std::string("pnp-test.board.xml");
                board["side"] = std::string("Top");
                board["enabled"] = true;
                board["location"]["x"] = 10.0;
                board["location"]["y"] = 20.0;
                board["location"]["z"] = 0.0;
                board["location"]["rotation"] = 0.0;
                answer["result"]["boards"] = JJson::array();
                answer["result"]["boards"].push(board);
                answer["result"]["file"] = std::string("pnp-test.job.xml");
            } else if (call == "boardPlacementLocation") {
                answer["result"]["x"] = 10.0 + request["location"]["x"].number();
                answer["result"]["y"] = 20.0 + request["location"]["y"].number();
                answer["result"]["z"] = 0.0;
                answer["result"]["rotation"] = 0.0;
            } else if (call == "readQrCode") {
                answer["result"] = std::string("XOUT");
            } else if (call == "setBoardEnabled") {
                boardEnabled = request["enabled"].boolean();
            } else if (call == "pipeline") {
                pipelineXml = request["xml"].str();
                pipelineCamera = request["camera"].str();
                JJson keyPoint = JJson::object();
                keyPoint["pt"]["x"] = 320.5;
                keyPoint["pt"]["y"] = 240.0;
                keyPoint["size"] = 12.0;
                JJson result = JJson::object();
                result["kind"] = std::string("List");
                result["text"] = std::string("[KeyPoint [pt={320.5, 240.0}]]");
                result["value"] = JJson::array();
                result["value"].push(keyPoint);
                answer["result"]["results"]["results"] = result;
            } else if (call == "showPipelineImage") {
                shownMs = int(request["ms"].number());
            }
            return answer;
        };
        write(dir / "openpnp.py", "from __future__ import absolute_import, division\n"
                                  "from org.openpnp.model import LengthUnit, Location\n"
                                  "from org.openpnp.util.UiUtils import submitUiMachineTask\n"
                                  "from javax.swing.JOptionPane import showMessageDialog\n"
                                  "def move():\n"
                                  "    nozzle = machine.defaultHead.defaultNozzle\n"
                                  "    location = nozzle.location\n"
                                  "    assert str(location) == '(1.000000, 2.000000, 0.000000, 0.000000 mm)', str(location)\n"
                                  "    nozzle.moveTo(location.add(Location(LengthUnit.Inches, 1, 0, 0, 0)))\n"
                                  "    assert nozzle.getName() == 'N1' and nozzle.part is None\n"
                                  "submitUiMachineTask(move)\n"
                                  "for feeder in machine.getFeeders():\n"
                                  "    assert feeder.getFeedCount() == 7\n"
                                  "    feeder.setFeedCount(0)\n"
                                  "from org.openpnp.util import Utils2D, VisionUtils\n"
                                  "assert scripting.getScriptsDirectory().toString()\n"
                                  "camera = machine.getDefaultHead().getDefaultCamera()\n"
                                  "for board in gui.jobTab.job.boardLocations:\n"
                                  "    location = Utils2D.calculateBoardPlacementLocation(board, Location(LengthUnit.Millimeters, 3, 3, 0, 0))\n"
                                  "    assert (location.x, location.y) == (13, 23), str(location)\n"
                                  "    camera.moveTo(location)\n"
                                  "    if VisionUtils.readQrCode(camera) is not None:\n"
                                  "        board.setEnabled(False)\n"
                                  "        gui.jobTab.refresh()\n"
                                  "from org.openpnp.vision.pipeline import CvPipeline\n"
                                  "from org.openpnp.util import OpenCvUtils\n"
                                  "pipeline = CvPipeline('<cv-pipeline><stages/></cv-pipeline>')\n"
                                  "pipeline.setProperty('camera', camera)\n"
                                  "pipeline.process()\n"
                                  "found = pipeline.getResult('results').model\n"
                                  "assert found[0].pt.x == 320.5 and found[0].getSize() == 12.0, found\n"
                                  "assert pipeline.getResult('none') is None\n"
                                  "gui.getCameraViews().getCameraView(camera).showFilteredImage(\n"
                                  "    OpenCvUtils.toBufferedImage(pipeline.getWorkingImage()), 'found', 1500)\n"
                                  "showMessageDialog(None, 'Hello!')\n");
        const bool openPnp = scripting.execute((dir / "openpnp.py").string(), JJson::object(), why);
        if (!openPnp) std::fprintf(stderr, "why: %s\n", why.c_str());
        assert(openPnp && moved.size() == 2 && std::abs(moved[size_t(0)].number() - 26.4) < 1e-9);
        assert(std::abs(moved[size_t(1)].number() - 13) < 1e-9 && !boardEnabled);
        assert(pipelineXml == "<cv-pipeline><stages/></cv-pipeline>" && pipelineCamera == "C1" && shownMs == 1500);
        assert(feedCountSet == 0 && dialog == "Hello!");
        // OpenPnP's JavaScript scripts, as Nashorn runs them: load, Packages, JavaImporter and with,
        // for each, print; the same machine.
        if (std::system("node -e '' >/dev/null 2>&1") == 0) {
            moved = JJson::array();
            feedCountSet = -1;
            dialog.clear();
            write(dir / "task.js", "function task(f) {\n"
                                   "  Packages.org.openpnp.util.UiUtils['submitUiMachineTask(Thrunnable)'](f);\n"
                                   "}\n");
            write(dir / "openpnp.js", "load(scripting.getScriptsDirectory().toString() + 'task.js');\n"
                                      "var imports = new JavaImporter(org.openpnp.model, org.openpnp.util);\n"
                                      "with (imports) {\n"
                                      "  task(function() {\n"
                                      "    var nozzle = machine.defaultHead.defaultNozzle;\n"
                                      "    var location = nozzle.location;\n"
                                      "    if (String(location) != '(1.000000, 2.000000, 0.000000, 0.000000 mm)') throw new Error(String(location));\n"
                                      "    nozzle.moveTo(location.add(new Location(LengthUnit.Inches, 1, 0, 0, 0)));\n"
                                      "  });\n"
                                      "}\n"
                                      "for each (var feeder in machine.getFeeders()) {\n"
                                      "  feeder.setFeedCount(0);\n"
                                      "  print('Reset ' + feeder.name);\n"
                                      "}\n"
                                      "javax.swing.JOptionPane.showMessageDialog(null, 'Hello!');\n");
            const bool js = scripting.execute((dir / "openpnp.js").string(), JJson::object(), why);
            if (!js) std::fprintf(stderr, "why: %s\n", why.c_str());
            assert(js && moved.size() == 1 && std::abs(moved[size_t(0)].number() - 26.4) < 1e-9);
            assert(feedCountSet == 0 && dialog == "Hello!");
        }
        scripting.api = nullptr;
        write(dir / "noone.py", "import jplacer\njplacer.positions()\n");
        assert(!scripting.execute((dir / "noone.py").string(), JJson::object(), why) && why.find("no machine to ask") != std::string::npos);
    }
    fs::remove_all(dir);
    return 0;
}
