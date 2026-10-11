// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// A feeder takes its part's packaging (JPFeederPackaging): the rotation, tape width and pitch into its own
// settings where its kind has them; loading takes the part's only packaging, else the one last chosen for it,
// else none (the user chooses); the same part again changes nothing; the feeder shows the packaging whose
// values it has, else its own.
// Tests check with assert(); a Release build must not compile it away.
#undef NDEBUG
#include <cassert>
#include <cmath>

#include "model/JPFeederPackaging.h"

#include <filesystem>

using namespace jf;

namespace {

constexpr auto kMm = JPLengthUnit::Millimeters;

bool near(double a, double b) { return std::fabs(a - b) < 1e-9; }

} // namespace

int main() {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "jplacer-test-feeder-packaging";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    JPConfiguration config(dir.string());
    auto one = std::make_shared<JPPart>();
    one->id = "C25804";
    one->packagings.push_back({ "Cut tape", 8, 4, "Paper", -90, 0, "" });
    config.addPart(one);
    auto two = std::make_shared<JPPart>();
    two->id = "R-10k";
    two->packagings.push_back({ "Cut tape", 8, 2, "Paper", 0, 0, "" });
    two->packagings.push_back({ "Reel", 12, 8, "Embossed", 180, 0, "" });
    config.addPart(two);

    // The only packaging: taken on loading.
    JPFeeder strip = JPFeeder::create("ReferenceStripFeeder", "");
    assert(JPFeederPackaging::takes(strip));
    const std::string said = JPFeederPackaging::load(config, strip, "C25804");
    assert(strip.partId() == "C25804" && said == "rotation -90°, 8 mm tape, 4 mm pitch");
    assert(near(strip.location().rotation(), -90) && near(strip.as<JPStripFeeder>()->tapeWidth.value(), 8)
           && near(strip.as<JPStripFeeder>()->partPitch.value(), 4));
    assert(JPFeederPackaging::matching(strip, *one) == 0);
    // Changed by hand: its own; the same part again keeps it.
    strip.as<JPStripFeeder>()->partPitch = JPLength(2, kMm);
    assert(JPFeederPackaging::matching(strip, *one) == -1);
    assert(JPFeederPackaging::load(config, strip, "C25804").empty() && near(strip.as<JPStripFeeder>()->partPitch.value(), 2));

    // Several and none chosen yet: nothing taken; one chosen is kept for the part and taken next time.
    JPFeeder a = JPFeeder::create("ReferenceStripFeeder", "");
    assert(JPFeederPackaging::load(config, a, "R-10k").empty());
    assert(JPFeederPackaging::choose(config, a, 1) == "rotation 180°, 12 mm tape, 8 mm pitch");
    assert(two->lastPackaging == 1 && JPFeederPackaging::matching(a, *two) == 1);
    JPFeeder b = JPFeeder::create("ReferenceStripFeeder", "");
    JPFeederPackaging::load(config, b, "R-10k");
    assert(JPFeederPackaging::matching(b, *two) == 1);

    // A kind with only a pitch (a drag feeder) takes only that; one with none takes nothing.
    JPFeeder drag = JPFeeder::create("ReferenceDragFeeder", "");
    assert(JPFeederPackaging::load(config, drag, "C25804") == "4 mm pitch");
    JPFeeder tray = JPFeeder::create("ReferenceTrayFeeder", "");
    assert(!JPFeederPackaging::takes(tray) && JPFeederPackaging::load(config, tray, "C25804").empty());
    // A tray's packaging gives no tape: only its rotation.
    auto inTray = std::make_shared<JPPart>();
    inTray->id = "QFP";
    inTray->packagings.push_back({ "Tray", 0, 0, "", 45, 0, "" });
    config.addPart(inTray);
    JPFeeder s2 = JPFeeder::create("ReferenceStripFeeder", "");
    assert(JPFeederPackaging::load(config, s2, "QFP") == "rotation 45°");
    // A Rapid feeder keeps its pitch in whole millimetres: 2.4 mm kept as 2, and that one still matches.
    auto fine = std::make_shared<JPPart>();
    fine->id = "FINE";
    fine->packagings.push_back({ "Reel", 8, 2.4, "", 0, 0, "" });
    config.addPart(fine);
    JPFeeder rapid = JPFeeder::create(JPRapidFeeder::kClass, "");
    assert(JPFeederPackaging::load(config, rapid, "FINE") == "2 mm pitch" && rapid.as<JPRapidFeeder>()->pitch == 2);
    assert(JPFeederPackaging::matching(rapid, *fine) == 0);
    assert(JPFeederPackaging::labels(*two)[1] == "2: Reel, 12 mm, 8 mm pitch, 180°" && JPFeederPackaging::labels(*inTray)[0] == "1: Tray, 45°");
    std::filesystem::remove_all(dir);
    return 0;
}
