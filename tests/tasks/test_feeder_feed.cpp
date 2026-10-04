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
    bool positionNozzle(const std::string&, const JPLocation&, std::string&) override { return true; }
    bool actuate(const std::string& name, double value, std::string&) override {
        actuated.push_back(name + "=" + std::to_string(int(value)));
        return true;
    }
    std::vector<std::string> actuated;
    bool park(std::string&) override { return true; }
    bool locateFiducial(const JPLocation&, double, const FiducialLook&, JPLocation&, std::string&) override { return false; }
    bool alignPart(const std::string&, const AlignRequest& rq, AlignResult& r, std::string&) override {
        r.nozzleAngle = rq.imageAngle;
        r.dx = alignDx;
        r.dy = 0;
        r.partAngle = rq.imageAngle + alignDa;
        ++aligns;
        return true;
    }
    double alignDx = 0, alignDa = 0;
    int    aligns = 0;
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
    assert(JPFeederFeed::feed(config, "S", "N1", machine, nullptr, why, empty) && machine.looks == 1);
    auto at = config.feeder("S")->pickLocation();
    assert(at && near(at->x(), 103.8) && near(at->y(), 52));
    // Each feed looks at its hole (no extrapolation distance).
    assert(JPFeederFeed::feed(config, "S", "N1", machine, nullptr, why, empty) && machine.looks == 2);
    // Skip next feed: the same part again, no look.
    config.feeder("S")->setFeedOptions(JPFeeder::FeedOptions::SkipNext);
    assert(JPFeederFeed::feed(config, "S", "N1", machine, nullptr, why, empty) && machine.looks == 2);

    // Picking from mid-strip with nothing seen yet: the first hole, then the one fed.
    JPFeeder& f = *config.feeder("S");
    f.visionLocation.reset();
    f.visionLocationReference.reset();
    f.setNumber("feed-count", 3);
    machine.looks = 0;
    assert(JPFeederFeed::feed(config, "S", "N1", machine, nullptr, why, empty) && machine.looks == 2);

    // An extrapolation distance of 12 mm: holes in between not looked at once under way.
    f.setLengthOf("extrapolation-distance", JPLength(12, JPLengthUnit::Millimeters));
    f.setLocationOf("last-hole-location", JPLocation(JPLengthUnit::Millimeters, 100, 10, 0, 0));
    machine.looks = 0;
    for (int i = 0; i < 4; ++i) assert(JPFeederFeed::feed(config, "S", "N1", machine, nullptr, why, empty));
    assert(machine.looks < 4);

    // An auto feeder: its feed actuator on a normal feed, not on a repeated
    // one; its post-pick actuator after the pick.
    {
        JPXmlElement ae;
        const bool ok = JPXmlReader::parse(R"(<feeder class="org.openpnp.machine.reference.feeder.ReferenceAutoFeeder" id="A" name="A" enabled="true" part-id="R1" feed-options="Normal" actuator-name="Feed" actuator-value="1.0" post-pick-actuator-name="Done" post-pick-actuator-value="0.0"><location units="Millimeters" x="1.0" y="2.0" z="-1.0" rotation="0.0"/></feeder>)", ae, error);
        assert(ok);
        config.addFeeder(JPFeeder::fromXml(ae));
        assert(JPFeederFeed::feed(config, "A", "N1", machine, nullptr, why, empty));
        assert((machine.actuated == std::vector<std::string> { "Feed=1" }));
        config.feeder("A")->setFeedOptions(JPFeeder::FeedOptions::SkipNext);
        assert(JPFeederFeed::feed(config, "A", "N1", machine, nullptr, why, empty) && machine.actuated.size() == 1);
        assert(config.feeder("A")->feedOptions() == JPFeeder::FeedOptions::Normal);
        assert(JPFeederFeed::postPick(config, "A", machine, nullptr, why) && machine.actuated.back() == "Done=0");
        assert(config.feeder("A")->pickLocation()->x() == 1);
    }

    // No hole: the strip's end.
    machine.holes = false;
    config.feeder("S")->visionLocation.reset();   // looked up again: the list moved when "A" was added
    assert(!JPFeederFeed::feed(config, "S", "N1", machine, nullptr, why, empty));
    assert(empty && why == "Unable to locate reference hole. End of strip?");
    return 0;
}
