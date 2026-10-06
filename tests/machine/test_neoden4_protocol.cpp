// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// The NeoDen 4's binary protocol, against a stand-in machine that answers
// each exchange as the NeoDen does: the checksum (CRC-16/CCITT's low byte),
// homing, moves in steps, each nozzle's Z and rotation, the speed, a feeder
// and a peeler (top half too), the air set and read (a reading past 110
// taken as below -128), and a failure answered by trying again.
// Tests check with assert(); a Release build must not compile it away.
#undef NDEBUG
#include <cassert>

#include "machine/JPNeoden4Protocol.h"
#include "FakeNeoden4.h"

#include <algorithm>

using namespace jf;

int main() {
    // OpenPnP's checksum: CRC-16/CCITT ("123456789" is 0x31C3), its low byte.
    const uint8_t digits[] = { '1', '2', '3', '4', '5', '6', '7', '8', '9' };
    assert(JPNeoden4Protocol::checksum(digits, sizeof digits) == 0xC3);

    FakeNeoden4 machine;
    JPNeoden4Protocol p(machine, 50);
    std::string why;

    // Homing: each nozzle up and turned back, let go, then the home command, done once ready.
    machine.busyPolls = 2;
    assert(p.home(why));
    assert(machine.payloads.size() == 10 && machine.payloads.back().announce == 0xc7 && machine.payloads.back().bytes[0] == 1);
    assert(machine.payloads[8].announce == 0xc1 && machine.payloads[8].bytes[3] == 0);   // every rotation let go

    // A move in steps, little-endian, negatives as two's complement.
    machine.payloads.clear();
    assert(p.moveSteps(12345, -678, why));
    assert(machine.payloads.size() == 1 && machine.payloads[0].announce == 0xc8);
    assert(int32At(machine.payloads[0].bytes, 0) == 12345 && int32At(machine.payloads[0].bytes, 4) == -678);

    // Z: its depth in microns; C: -tenths of a degree; the speed 10 .. 130.
    machine.payloads.clear();
    assert(p.moveZ(3, -4.5, why) && p.moveC(2, 90, why) && p.setMoveSpeed(0.5, why));
    const auto& z = machine.payloads[0].bytes;
    assert((z[0] | z[1] << 8) == 4500 && z[2] == 0x64 && z[3] == 3);
    const auto& c = machine.payloads[1].bytes;
    assert(int16_t(c[0] | c[1] << 8) == -900 && c[2] == 0x32 && c[3] == 2);
    const auto& v = machine.payloads[2].bytes;
    assert((v[0] | v[1] << 8) == 70 && v[2] == 0x09 && v[4] == 0xc8);

    // A peeler: the bottom half's by its id, the top half's from 20, counted from 1 there.
    machine.payloads.clear();
    assert(p.peel(5, 30, 40, why) && p.peel(21, 30, 40, why));
    assert(machine.payloads[0].announce == 0xcc && machine.payloads[0].bytes[0] == 5 && machine.payloads[0].bytes[1] == 40);
    assert(machine.payloads[1].announce == 0xce && machine.payloads[1].bytes[0] == 2 && machine.payloads[1].bytes[2] == 30);

    // A feeder fed: its strength and rate, unannounced after its id; its id changed (0..99 only).
    machine.payloads.clear();
    assert(p.feed(5, 50, 4, why));
    assert(machine.payloads.size() == 1 && machine.payloads[0].announce == 0x3f && machine.payloads[0].bytes[0] == 50
           && machine.payloads[0].bytes[1] == 4);
    assert(std::count(machine.written.end() - 6, machine.written.end(), uint8_t(0x46 + 5)) >= 1);
    assert(p.changeFeederId(5, 7, why) && machine.payloads.back().bytes[0] == 7 && machine.payloads.back().bytes[7] == 1);
    assert(!p.changeFeederId(5, 100, why) && why == "changeFeederId newId must be between 0-99.");

    // The air: full vacuum sent as 0x80; read back, a reading past 110 as below -128.
    machine.payloads.clear();
    assert(p.setAir(1, -128, why) && machine.payloads[0].bytes[0] == 0x80 && machine.payloads[0].bytes[1] == 1);
    int air = 0;
    assert(p.readAir(1, air, why) && air == -100);
    assert(p.readAir(4, air, why) && air == -136);

    // A spoilt answer: made again, and done.
    machine.payloads.clear();
    machine.refuse = 1;
    assert(p.lightsDown(3, why));
    assert(!machine.payloads.empty() && machine.payloads.back().announce == 0xc4 && machine.payloads.back().bytes[0] == 3);
    // A move is not made again by itself: its caller does.
    machine.refuse = 1;
    assert(!p.moveZ(1, 0, why) && !why.empty());
    machine.flushInput();
    return 0;
}
