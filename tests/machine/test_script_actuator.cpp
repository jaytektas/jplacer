// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// OpenPnP's ScriptActuator: switched, its script is run told actuateBoolean;
// set, actuateDouble (a Double one) or actuateString; with no scripts to run,
// or a script failing, it is not actuated and says why.
// Tests check with assert(); a Release build must not compile it away.
#undef NDEBUG
#include <cassert>

#include "machine/JPCell.h"

#include <filesystem>
#include <fstream>
#include <string>

using namespace jf;
namespace fs = std::filesystem;

int main() {
    const fs::path dir = fs::temp_directory_path() / "jplacer-test-script-actuator";
    fs::remove_all(dir);
    auto scripting = std::make_shared<JPScripting>(dir.string());
    const fs::path log = dir / "log.txt";
    fs::create_directories(dir / "Actuators");
    std::ofstream(dir / "Actuators" / "lamp.sh") << "echo \"$JPLACER_GLOBALS\" >> " << log.string() << "\n";
    std::ofstream(dir / "Actuators" / "broken.sh") << "exit 2\n";

    JPCellConfig config;
    config.name = "Script";
    JPActuatorConfig lamp;
    lamp.id = "L";
    lamp.name = "Lamp";
    lamp.scriptName = "Actuators/lamp.sh";
    lamp.valueType = JPActuatorConfig::ValueType::Number;
    config.actuators.push_back(lamp);
    JPActuatorConfig label = lamp;
    label.id = "S";
    label.name = "Label";
    label.valueType = JPActuatorConfig::ValueType::Text;
    config.actuators.push_back(label);
    JPActuatorConfig broken = lamp;
    broken.id = "B";
    broken.name = "Broken";
    broken.scriptName = "Actuators/broken.sh";
    config.actuators.push_back(broken);
    assert(config.problems().empty());
    assert(lamp.canSwitch() && lamp.canSet());

    std::string why;
    {
        JPCell cell(config, {});
        assert(!cell.switchActuatorAndWait("L", true, why) && why.find("no scripts") != std::string::npos);
    }
    JPCell cell(config, {});
    cell.setScripting(scripting);
    assert(cell.switchActuatorAndWait("L", true, why));
    assert(cell.setActuatorAndWait("L", "2.5", why));
    assert(cell.setActuatorAndWait("S", "red", why));
    assert(!cell.switchActuatorAndWait("B", true, why) && why.find("exit 2") != std::string::npos);
    {
        std::ifstream in(log);
        std::string a, b, c, d;
        std::getline(in, a);
        std::getline(in, b);
        std::getline(in, c);
        assert(a == R"({"actuateBoolean":true,"actuator":"Lamp"})");
        assert(b == R"({"actuateDouble":2.5,"actuator":"Lamp"})");
        assert(c == R"({"actuateString":"red","actuator":"Label"})");
        assert(!std::getline(in, d));
    }
    fs::remove_all(dir);
    return 0;
}
