// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// A nozzle tip's changer steps run backwards to unload it: in by way of safe Z
// to where loading ended, each move back to where the one before it went as
// the move it undoes went (its speed, by way of safe Z or not, compensated or
// not); actuators switched the other way; a move down from safe Z undone by
// going up, then across. What is wrong with a list of steps, and steps saved
// before a move said whether it goes by way of safe Z.
// Tests check with assert(); a Release build must not compile it away.
#undef NDEBUG
#include <cassert>

#include "machine/JPCellConfig.h"

#include <cstdio>

using namespace jf;
using Kind = JPChangerStep::Kind;

namespace {

JPChangerStep move(std::optional<double> x, std::optional<double> y, std::optional<double> z, double speed,
                   bool safeZ = false, bool backlash = false) {
    JPChangerStep s;
    s.x = x;
    s.y = y;
    s.z = z;
    s.speed = speed;
    s.safeZ = safeZ;
    s.backlash = backlash;
    return s;
}

JPChangerStep step(Kind k) {
    JPChangerStep s;
    s.kind = k;
    return s;
}

bool at(const JPChangerStep& s, std::optional<double> x, std::optional<double> y, std::optional<double> z, double speed,
        bool safeZ = false, bool backlash = false) {
    return s.kind == Kind::Move && s.x == x && s.y == y && s.z == z && s.speed == speed && s.safeZ == safeZ
        && s.backlash == backlash;
}

} // namespace

int main() {
    // OpenPnP's four places: down into the slot, across, up.
    const std::vector<JPChangerStep> openpnp = { move(-16, 130, 0, 1, true), move(-16, 130, -27, 0.5),
                                                 move(4, 130, -27, 0.5), move(4, 130, 0, 1) };
    std::vector<JPChangerStep> back = JPNozzleTipConfig::reversed(openpnp);
    assert(back.size() == 4);
    assert(at(back[0], 4, 130, 0, 1, true));    // in to where loading ended, by way of safe Z
    assert(at(back[1], 4, 130, -27, 1));        // down, as fast as loading came up
    assert(at(back[2], -16, 130, -27, 0.5));    // across
    assert(at(back[3], -16, 130, 0, 0.5));      // up
    std::printf("  [OK] OpenPnP's changer backwards\n");

    // A lock opened between the moves is closed again in the same place; a
    // move giving only Z keeps X and Y; a wait stays a wait.
    JPChangerStep unlock = step(Kind::Actuator);
    unlock.actuatorId = "LOCK";
    unlock.on = true;
    back = JPNozzleTipConfig::reversed({ move(10, 20, -5, 1, true), unlock, step(Kind::Wait), move({}, {}, -9, 0.25) });
    assert(back.size() == 4);
    assert(at(back[0], 10, 20, -9, 0.25, true));
    assert(at(back[1], 10, 20, -5, 0.25));
    assert(back[2].kind == Kind::Wait);
    assert(back[3].kind == Kind::Actuator && back[3].actuatorId == "LOCK" && !back[3].on);
    std::printf("  [OK] actuators and partial moves\n");

    // Up to safe Z between two places: undone by up, across, then down.
    back = JPNozzleTipConfig::reversed({ move(1, 2, -3, 1, true), step(Kind::SafeZ), move(50, 60, -7, 0.5) });
    assert(back.size() == 4);
    assert(at(back[0], 50, 60, -7, 0.5, true));
    assert(back[1].kind == Kind::SafeZ);
    assert(at(back[2], 1, 2, std::nullopt, 0.5));
    assert(at(back[3], 1, 2, -3, 1));
    assert(JPNozzleTipConfig::reversed({}).empty());
    std::printf("  [OK] through safe Z\n");

    // A move by way of safe Z, compensated, between two holders: undone the same way back, the move after it
    // (straight, not compensated) as it was.
    back = JPNozzleTipConfig::reversed({ move(1, 2, -3, 1, true), move(40, 2, -3, 0.5, true, true), move(44, 2, -3, 0.25) });
    assert(back.size() == 3);
    assert(at(back[0], 44, 2, -3, 0.25, true));
    assert(at(back[1], 40, 2, -3, 0.25));
    assert(at(back[2], 1, 2, -3, 0.5, true, true));
    std::printf("  [OK] each move's safe Z and backlash\n");

    // A list must start with a move saying where; its own unloading steps too.
    JPNozzleTipConfig tip;
    tip.name = "503";
    tip.loadSteps = { step(Kind::Wait), move(1, 2, 3, 1) };
    assert(tip.problems().size() == 1);
    tip.loadSteps = { move(1, 2, {}, 1) };
    assert(tip.problems().size() == 1);
    tip.loadSteps = { move(1, 2, 3, 1) };
    assert(tip.problems().size() == 1);   // not by way of safe Z: the nozzle may be anywhere
    tip.loadSteps = { move(1, 2, 3, 1, true) };
    assert(tip.problems().empty());
    tip.unloadReversesLoad = false;
    tip.unloadSteps = { step(Kind::Ask) };
    assert(tip.problems().size() == 1);
    // And through the cell file.
    tip.unloadSteps = { move(1, 2, 3, 1, true, true), unlock };
    JPCellConfig cell;
    cell.nozzleTips.push_back(tip);
    assert(cell.problems().size() == 1);   // LOCK is not in the cell
    JPCellConfig again;
    std::string error;
    assert(again.fromJson(cell.toJson(), error));
    assert(again.toJson().dump() == cell.toJson().dump());
    assert(again.nozzleTips[0].unloadSteps[1].actuatorId == "LOCK" && !again.nozzleTips[0].unloadReversesLoad);
    assert(!again.nozzleTips[0].loadSteps[0].rotation);
    assert(again.nozzleTips[0].unloadSteps[0].safeZ && again.nozzleTips[0].unloadSteps[0].backlash);
    std::printf("  [OK] what is wrong, and the cell file\n");

    // Saved before: the first move by way of safe Z, the others straight; none compensated.
    JJson old = JJson::object();
    old["id"] = "T";
    old["load"] = JJson::array();
    for (double z : { 0.0, -27.0 }) {
        JJson m = JJson::object();
        m["kind"] = "move";
        m["x"] = 1.0;
        m["y"] = 2.0;
        m["z"] = z;
        m["speed"] = 1.0;
        old["load"].push(m);
    }
    const JPNozzleTipConfig was = JPNozzleTipConfig::fromJson(old);
    assert(was.loadSteps.size() == 2 && was.loadSteps[0].safeZ && !was.loadSteps[1].safeZ);
    assert(!was.loadSteps[0].backlash && was.problems().empty());
    std::printf("  [OK] steps saved before\n");
    std::printf("All changer tests passed.\n");
    return 0;
}
