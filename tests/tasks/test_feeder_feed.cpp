// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// A strip feeder's feed with vision, as OpenPnP's: the hole fed looked at
// (the first one too, when picking starts mid-strip), the parts following
// where it was found; with an extrapolation distance, holes in between
// skipped; a hole not found is the strip's end.
// Tests check with assert(); a Release build must not compile it away.
#undef NDEBUG
#include <cassert>

#include "model/JPConfiguration.h"
#include "openpnp/JPXmlReader.h"
#include "tasks/JPFeederFeed.h"

#include <cmath>
#include <filesystem>

using namespace jf;
namespace fs = std::filesystem;

namespace {

bool near(double a, double b) { return std::abs(a - b) < 1e-6; }

// Finds every hole `shiftX` mm off where it is looked for; or none.
class HoleMachine : public JPJobMachine {
public:
    double shiftX = 0.3;
    bool   holes = true;
    int    looks = 0;
    std::vector<Nozzle> nozzles() const override { return {}; }
    std::vector<std::pair<std::string, std::string>> tips() const override { return {}; }
    std::optional<JPLocation> cameraLocation() const override { return std::nullopt; }
    bool safeZ(std::string&) override { return true; }
    bool changeTip(const std::string&, const std::string&, std::string&) override { return true; }
    bool rotate(const std::string&, double, std::string&) override { return true; }
    bool pick(const std::string&, const JPLocation&, std::string&) override { return true; }
    bool place(const std::string&, const JPLocation&, std::string&) override { return true; }
    bool discard(const std::string&, std::string&) override { return true; }
    bool park(std::string&) override { return true; }
    bool locateFiducial(const JPLocation&, double, JPLocation&, std::string&) override { return false; }
    bool locateHole(const JPLocation& nominal, double diameterMm, double searchMm, double, double, JPLocation& found,
                    std::string& why) override {
        assert(near(diameterMm, 1.5) && near(searchMm, 2));
        ++looks;
        if (!holes) {
            why = "no round mark";
            return false;
        }
        found = nominal.add(JPLocation(JPLengthUnit::Millimeters, shiftX, 0, 0, 0));
        return true;
    }
};

const char* kStrip = R"(<feeder class="org.openpnp.machine.reference.feeder.ReferenceStripFeeder" id="S" name="S" enabled="true" part-id="R1" feed-options="Normal" standard-eia-481="true" vision-enabled="true" feed-count="0" max-feed-count="0">
   <location units="Millimeters" x="0.0" y="0.0" z="0.0" rotation="0.0"/>
   <reference-hole-location units="Millimeters" x="100.0" y="50.0" z="-20.0" rotation="0.0"/>
   <last-hole-location units="Millimeters" x="100.0" y="34.0" z="0.0" rotation="0.0"/>
   <part-pitch value="4.0" units="Millimeters"/>
   <tape-width value="8.0" units="Millimeters"/>
   <extrapolation-distance value="0.0" units="Millimeters"/>
</feeder>)";

} // namespace

int main() {
    const fs::path dir = fs::temp_directory_path() / "jplacer-test-feeder-feed";
    fs::remove_all(dir);
    fs::create_directories(dir);
    JPConfiguration config(dir.string());
    JPXmlElement e;
    std::string error;
    const bool parsed = JPXmlReader::parse(kStrip, e, error);
    assert(parsed);
    config.addFeeder(JPFeeder::fromXml(e));
    HoleMachine machine;
    std::string why;
    bool empty = false;

    // The first feed: the first hole looked at, found 0.3 mm off; the part follows it.
    assert(JPFeederFeed::feed(config, "S", machine, nullptr, why, empty) && machine.looks == 1);
    auto at = config.feeder("S")->pickLocation();
    assert(at && near(at->x(), 103.8) && near(at->y(), 52));
    // Each feed looks at its hole (no extrapolation distance).
    assert(JPFeederFeed::feed(config, "S", machine, nullptr, why, empty) && machine.looks == 2);
    // Skip next feed: the same part again, no look.
    config.feeder("S")->setFeedOptions(JPFeeder::FeedOptions::SkipNext);
    assert(JPFeederFeed::feed(config, "S", machine, nullptr, why, empty) && machine.looks == 2);

    // Picking from mid-strip with nothing seen yet: the first hole, then the one fed.
    JPFeeder& f = *config.feeder("S");
    f.visionLocation.reset();
    f.visionLocationReference.reset();
    f.setNumber("feed-count", 3);
    machine.looks = 0;
    assert(JPFeederFeed::feed(config, "S", machine, nullptr, why, empty) && machine.looks == 2);

    // An extrapolation distance of 12 mm: holes in between not looked at once under way.
    f.setLengthOf("extrapolation-distance", JPLength(12, JPLengthUnit::Millimeters));
    f.setLocationOf("last-hole-location", JPLocation(JPLengthUnit::Millimeters, 100, 10, 0, 0));
    machine.looks = 0;
    for (int i = 0; i < 4; ++i) assert(JPFeederFeed::feed(config, "S", machine, nullptr, why, empty));
    assert(machine.looks < 4);

    // No hole: the strip's end.
    machine.holes = false;
    f.visionLocation.reset();
    assert(!JPFeederFeed::feed(config, "S", machine, nullptr, why, empty));
    assert(empty && why == "Unable to locate reference hole. End of strip?");
    return 0;
}
