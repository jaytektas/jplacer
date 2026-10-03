// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// A nozzle tip's changer steps run backwards to unload it: each move back to
// where the one before it went, at the speed of the move it undoes; actuators
// switched the other way; a move down from safe Z undone by going up, then
// across. And what is wrong with a list of steps.
// Tests check with assert(); a Release build must not compile it away.
#undef NDEBUG
#include <cassert>

#include "machine/JPCellConfig.h"

#include <cstdio>

using namespace jf;
using Kind = JPChangerStep::Kind;

namespace {

JPChangerStep move(std::optional<double> x, std::optional<double> y, std::optional<double> z, double speed) {
    JPChangerStep s;
    s.x = x;
    s.y = y;
    s.z = z;
    s.speed = speed;
    return s;
}

JPChangerStep step(Kind k) {
    JPChangerStep s;
    s.kind = k;
    return s;
}

bool at(const JPChangerStep& s, std::optional<double> x, std::optional<double> y, std::optional<double> z, double speed) {
    return s.kind == Kind::Move && s.x == x && s.y == y && s.z == z && s.speed == speed;
}

} // namespace

int main() {
    // OpenPnP's four places: down into the slot, across, up.
    const std::vector<JPChangerStep> openpnp = { move(-16, 130, 0, 1), move(-16, 130, -27, 0.5),
                                                 move(4, 130, -27, 0.5), move(4, 130, 0, 1) };
    std::vector<JPChangerStep> back = JPNozzleTipConfig::reversed(openpnp);
    assert(back.size() == 4);
    assert(at(back[0], 4, 130, 0, 1));          // in to where loading ended
    assert(at(back[1], 4, 130, -27, 1));        // down, as fast as loading came up
    assert(at(back[2], -16, 130, -27, 0.5));    // across
    assert(at(back[3], -16, 130, 0, 0.5));      // up
    std::printf("  [OK] OpenPnP's changer backwards\n");

    // A lock opened between the moves is closed again in the same place; a
    // move giving only Z keeps X and Y; a wait stays a wait.
    JPChangerStep unlock = step(Kind::Actuator);
    unlock.actuatorId = "LOCK";
    unlock.on = true;
    back = JPNozzleTipConfig::reversed({ move(10, 20, -5, 1), unlock, step(Kind::Wait), move({}, {}, -9, 0.25) });
    assert(back.size() == 4);
    assert(at(back[0], 10, 20, -9, 0.25));
    assert(at(back[1], 10, 20, -5, 0.25));
    assert(back[2].kind == Kind::Wait);
    assert(back[3].kind == Kind::Actuator && back[3].actuatorId == "LOCK" && !back[3].on);
    std::printf("  [OK] actuators and partial moves\n");

    // Up to safe Z between two places: undone by up, across, then down.
    back = JPNozzleTipConfig::reversed({ move(1, 2, -3, 1), step(Kind::SafeZ), move(50, 60, -7, 0.5) });
    assert(back.size() == 4);
    assert(at(back[0], 50, 60, -7, 0.5));
    assert(back[1].kind == Kind::SafeZ);
    assert(at(back[2], 1, 2, std::nullopt, 0.5));
    assert(at(back[3], 1, 2, -3, 1));
    assert(JPNozzleTipConfig::reversed({}).empty());
    std::printf("  [OK] through safe Z\n");

    // A list must start with a move saying where; its own unloading steps too.
    JPNozzleTipConfig tip;
    tip.name = "503";
    tip.loadSteps = { step(Kind::Wait), move(1, 2, 3, 1) };
    assert(tip.problems().size() == 1);
    tip.loadSteps = { move(1, 2, {}, 1) };
    assert(tip.problems().size() == 1);
    tip.loadSteps = { move(1, 2, 3, 1) };
    assert(tip.problems().empty());
    tip.unloadReversesLoad = false;
    tip.unloadSteps = { step(Kind::Ask) };
    assert(tip.problems().size() == 1);
    // And through the cell file.
    tip.unloadSteps = { move(1, 2, 3, 1), unlock };
    JPCellConfig cell;
    cell.nozzleTips.push_back(tip);
    assert(cell.problems().size() == 1);   // LOCK is not in the cell
    JPCellConfig again;
    std::string error;
    assert(again.fromJson(cell.toJson(), error));
    assert(again.toJson().dump() == cell.toJson().dump());
    assert(again.nozzleTips[0].unloadSteps[1].actuatorId == "LOCK" && !again.nozzleTips[0].unloadReversesLoad);
    assert(!again.nozzleTips[0].loadSteps[0].rotation);
    std::printf("  [OK] what is wrong, and the cell file\n");
    std::printf("All changer tests passed.\n");
    return 0;
}
