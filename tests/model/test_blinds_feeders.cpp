// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// OpenPnP's BlindsFeeder model: the holder's frame from its fiducials (and
// turned), the tape length and extent they give, the pockets and where each
// is picked, the cover's state, the feeders on one holder sharing its
// settings and numbered across it, the whole holder moved by fiducial 1, the
// OCR label's place, the covers to open and the job's visits.
// Tests check with assert(); a Release build must not compile it away.
#undef NDEBUG
#include <cassert>

#include "model/JPBlindsFeeders.h"
#include "model/JPConfiguration.h"

#include <cmath>
#include <filesystem>

using namespace jf;
namespace fs = std::filesystem;

namespace {
constexpr JPLengthUnit kMm = JPLengthUnit::Millimeters;
bool near(double a, double b) { return std::abs(a - b) < 1e-6; }
JPLocation at(double x, double y) { return JPLocation(kMm, x, y, 0, 0); }
}

int main() {
    const fs::path dir = fs::temp_directory_path() / "jplacer-test-blinds-feeders";
    fs::remove_all(dir);
    fs::create_directories(dir);
    JPConfiguration c(dir.string());
    JPFeeder a = JPFeeder::create("org.openpnp.machine.reference.feeder.BlindsFeeder", "R1");
    const std::string ida = a.id();
    a.setEnabled(true);
    a.setLocation(JPLocation(kMm, 0, 0, -10, 0));
    c.addFeeder(std::move(a));
    // Fiducials 80 mm along and 40 mm across: the tape length and extent; the frame square to the machine.
    JPBlindsFeeders::setFiducial(c, ida, 1, at(10, 20));
    JPBlindsFeeders::setFiducial(c, ida, 2, at(90, 20));
    JPBlindsFeeders::setFiducial(c, ida, 3, at(10, 60));
    JPFeeder* fa = c.feeder(ida);
    assert(near(fa->lengthOf("tape-length", JPLength(0, kMm)).value(), 80));
    assert(near(fa->lengthOf("feeder-extent", JPLength(0, kMm)).value(), 40));
    // 4 mm pockets on the centerline 10 mm across: 20 of them, the first 2 mm in.
    fa->setLengthOf("pocket-pitch", JPLength(4, kMm));
    fa->setLengthOf("pocket-centerline", JPLength(10, kMm));
    JPBlindsFeeders::recalculateGeometry(*fa);
    assert(fa->number("pocket-count") == 20 && fa->number("first-pocket") == 1 && fa->number("last-pocket") == 20);
    assert(near(JPBlindsFeeders::pocketDistanceMm(*fa), 2));
    JPLocation p = JPBlindsFeeders::pickLocation(*fa, 1);
    assert(near(p.x(), 12) && near(p.y(), 30) && near(p.z(), -10) && near(p.rotation(), 180));
    p = JPBlindsFeeders::pickLocation(*fa, 3);
    assert(near(p.x(), 20));
    // Back into the frame.
    const JPLocation back = JPBlindsFeeders::machineToFeeder(*fa, p);
    assert(near(back.x(), 10) && near(back.y(), 10));
    // On the holder; and off it.
    assert(JPBlindsFeeders::isLocationInFeeder(*fa, at(50, 40), false) && !JPBlindsFeeders::isLocationInFeeder(*fa, at(50, 70), false));
    assert(!JPBlindsFeeders::isLocationInFeeder(*fa, at(50, 40), true) && JPBlindsFeeders::isLocationInFeeder(*fa, at(11, 20), true));

    // The cover: unknown, then where the pockets are (open), half a pitch off (closed).
    assert(!JPBlindsFeeders::coverState(*fa, true) && !JPBlindsFeeders::coverState(*fa, false));
    fa->blinds.coverPositionMm = 2;
    assert(JPBlindsFeeders::coverState(*fa, true) && !JPBlindsFeeders::coverState(*fa, false));
    fa->blinds.coverPositionMm = 0;
    assert(JPBlindsFeeders::coverState(*fa, false));
    fa->blinds.coverPositionMm.reset();
    // Its cover to open (it is enabled and not known open), and to close (nor known closed).
    assert(JPBlindsFeeders::coversToActuate(c, { "OpenOnJobStart" }, true) == std::vector<std::string> { ida });
    assert(JPBlindsFeeders::coversToActuate(c, { "OpenOnJobStart" }, false) == std::vector<std::string> { ida });
    assert(JPBlindsFeeders::coversToActuate(c, { "Manual" }, true).empty());
    // A job visits it: not calibrated, and to be opened.
    assert(JPBlindsFeeders::jobPreparationLocation(*fa).has_value());

    // The OCR label in front of the tape's start.
    const auto corners = JPBlindsFeeders::ocrRegionCorners(*fa, 10);
    assert(near(corners[0].x(), -2) && near(corners[1].x(), -22) && near(corners[0].y(), 10 - 1.0625 - 5) && near(corners[2].y(), 10 - 1.0625 + 5));

    // A second feeder on the same holder: its first fiducial fix takes the holder's settings; the lanes numbered by centerline.
    JPFeeder b = JPFeeder::create("org.openpnp.machine.reference.feeder.BlindsFeeder", "R2");
    const std::string idb = b.id();
    c.addFeeder(std::move(b));
    JPBlindsFeeders::setFiducial(c, idb, 1, at(10, 20));
    JPFeeder* fb = c.feeder(idb);
    assert(near(fb->locationOf("fiducial-2-location").x(), 90) && near(fb->lengthOf("tape-length", JPLength(0, kMm)).value(), 80));
    assert(fb->number("feeders-total") == 2 && fb->number("feeder-no") == 1 && c.feeder(ida)->number("feeder-no") == 2);
    // Fiducial 1 moved 5 mm: the whole holder moves, the other feeder with it.
    JPBlindsFeeders::setFiducial(c, ida, 1, at(15, 20));
    assert(near(c.feeder(ida)->locationOf("fiducial-2-location").x(), 95) && near(c.feeder(ida)->locationOf("fiducial-3-location").x(), 15));
    assert(near(c.feeder(idb)->locationOf("fiducial-1-location").x(), 15) && near(c.feeder(idb)->locationOf("fiducial-2-location").x(), 95));
    // Fiducial 2 turned 90° about fiducial 1: fiducial 3 turned with it; the frame turned.
    JPBlindsFeeders::setFiducial(c, ida, 2, at(15, 100));
    fa = c.feeder(ida);
    assert(near(fa->locationOf("fiducial-3-location").x(), -25) && near(fa->locationOf("fiducial-3-location").y(), 20));
    p = JPBlindsFeeders::pickLocation(*fa, 1);
    assert(near(p.x(), 15 - 10) && near(p.y(), 20 + 2) && near(p.rotation(), 270));

    // Groups: the default for location names; renamed for the whole holder.
    assert(JPBlindsFeeders::groupName(*fa) == "Default");
    JPBlindsFeeders::setGroupName(c, ida, "Left");
    assert(JPBlindsFeeders::groupName(*c.feeder(ida)) == "Left" && JPBlindsFeeders::groupName(*c.feeder(idb)) == "Left");
    assert((JPBlindsFeeders::groupNames(c) == std::vector<std::string> { "Default", "Left" }));
    JPBlindsFeeders::setGroupName(c, ida, "location");
    assert(JPBlindsFeeders::groupName(*c.feeder(ida)) == "Default");
    return 0;
}
