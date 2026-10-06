// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// The NeoDen 4 link, against a stand-in machine: identified as the neoden4
// profile says, moves refused before homing, homing once (again only the
// offsets forgotten), G92 after homing only saying where the axes are and
// later offsetting X and Y, a move's rotations, Zs and then X and Y (only
// those that change) in the machine's steps at the feed rate's share of
// 250 mm/s, the vacuum on and off, the air read, the lights, and a NeoDen 4
// feeder fed and peeled; the status in the profile's form.
// Tests check with assert(); a Release build must not compile it away.
#undef NDEBUG
#include <cassert>

#include "FakeNeoden4.h"
#include "machine/JPFirmwareProfile.h"
#include "machine/JPNeoden4Link.h"

using namespace jf;

namespace {

// Every reply to `line`, the last first checked to be "ok".
std::vector<std::string> ask(JPNeoden4Link& link, const std::string& line) {
    assert(link.write(line + "\n"));
    std::vector<std::string> replies;
    while (const auto r = link.readLine(0)) replies.push_back(*r);
    return replies;
}

bool ok(JPNeoden4Link& link, const std::string& line) {
    const auto r = ask(link, line);
    return !r.empty() && r.back() == "ok";
}

} // namespace

int main() {
    FakeNeoden4 machine;
    JPNeoden4Link link({}, machine, 50);
    std::string error;
    assert(link.open(error) && link.isOpen());

    JPFirmwareProfile profile;
    assert(profile.load(JPLACER_PROFILES_DIR "/neoden4.json", error));
    const auto id = ask(link, profile.identifyCommand());
    assert(profile.identifies(id) && id.back() == "ok");

    // Not homed: no move.
    const auto refused = ask(link, "G1 X10 Y10 F6000");
    assert(refused.size() == 1 && refused[0].rfind("error: NeoDen4Driver moveTo: Machine must be homed", 0) == 0);
    assert(profile.errorIn(refused[0]));

    // Homed, and told where homing leaves it: no offset.
    assert(ok(link, "G28"));
    const size_t homing = machine.payloads.size();
    assert(homing == 10);
    assert(ok(link, "G92 X-437 Y437 Z0 A0"));

    // A move: X and Y in steps (hundredths, scaled) at its share of 250 mm/s.
    machine.payloads.clear();
    assert(ok(link, "G1 X100 Y-50 F7500"));
    assert(machine.payloads.size() == 2 && machine.payloads[0].announce == 0xc6 && machine.payloads[1].announce == 0xc8);
    const auto speed = machine.payloads[0].bytes;
    assert((speed[0] | speed[1] << 8) == 70);   // 7500 mm/min = 125 mm/s: half
    assert(int32At(machine.payloads[1].bytes, 0) == int32_t(100 * JPNeoden4Link::kScaleX * 100));
    assert(int32At(machine.payloads[1].bytes, 4) == int32_t(-50 * JPNeoden4Link::kScaleY * 100));

    // Rotation first, then Z; X unchanged and Y unchanged: no X/Y move. The feed rate stays.
    machine.payloads.clear();
    assert(ok(link, "G1 X100 Y-50 U-4.5 B90"));
    assert(machine.payloads.size() == 2 && machine.payloads[0].announce == 0xc1 && machine.payloads[1].announce == 0xc2);
    assert(machine.payloads[0].bytes[3] == 2 && machine.payloads[1].bytes[3] == 2);

    // The status: the positions last sent, in the profile's letters.
    const auto st = ask(link, "?");
    assert(st.size() == 2);
    const auto parsed = profile.parseStatus(st[0]);
    assert(parsed && parsed->state == "Idle" && parsed->positions.at("X") == 100 && parsed->positions.at("U") == -4.5
           && parsed->positions.at("B") == 90);

    // A G92 after that: X and Y offset (X 10 is now where X 100 was).
    assert(ok(link, "G92 X10"));
    machine.payloads.clear();
    assert(ok(link, "G0 X20"));
    assert(int32At(machine.payloads[1].bytes, 0) == int32_t(110 * JPNeoden4Link::kScaleX * 100));
    const auto fast = machine.payloads[0].bytes;
    assert((fast[0] | fast[1] << 8) == 130);   // G0: full speed

    // Homing again: not done again, the offsets forgotten.
    machine.payloads.clear();
    assert(ok(link, "G28") && machine.payloads.empty());
    assert(ok(link, "G92 X-437 Y437"));
    assert(ok(link, "G1 X100 Y-50 F7500"));
    assert(int32At(machine.payloads[1].bytes, 0) == int32_t(100 * JPNeoden4Link::kScaleX * 100));

    // The air: vacuum on (-128), off (20, then 0); read.
    machine.payloads.clear();
    assert(ok(link, "VACUUM 2 ON") && ok(link, "vacuum 2 off"));
    assert(machine.payloads.size() == 3 && machine.payloads[0].bytes[0] == 0x80 && machine.payloads[1].bytes[0] == 20
           && machine.payloads[2].bytes[0] == 0 && machine.payloads[2].bytes[1] == 2);
    const auto air = ask(link, "AIR? 1");
    assert(air.size() == 2 && air[0] == "AIR:-100");

    // Lights, rails, a NeoDen 4 feeder: fed 4, peeled 50 % of 5 x 4.
    machine.payloads.clear();
    assert(ok(link, "LIGHTS DOWN 3") && machine.payloads.back().announce == 0xc4 && machine.payloads.back().bytes[0] == 3);
    assert(ok(link, "NEOFEED 5 50 5 30 50 4"));
    assert(machine.payloads[machine.payloads.size() - 2].announce == 0x3f && machine.payloads.back().announce == 0xcc
           && machine.payloads.back().bytes[1] == 10);
    assert(ask(link, "NEOFEED 5 50 5 30 50 0").back().rfind("error", 0) == 0);

    // Not a NeoDen command: an error, as a controller answers.
    assert(ask(link, "M3 S1000").back() == "error: 'M3' is not a NeoDen 4 command");
    link.close();
    assert(!link.isOpen());
    return 0;
}
