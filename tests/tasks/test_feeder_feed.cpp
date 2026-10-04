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
#include "tasks/JPFeederActions.h"
#include "tasks/JPFeederFeed.h"

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <map>

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
    // What a read gives: by actuator name.
    std::map<std::string, std::string> readings;
    bool readActuator(const std::string& name, double parameter, std::string& value, std::string& why) override {
        actuated.push_back("read " + name + "(" + std::to_string(int(parameter)) + ")");
        const auto r = readings.find(name);
        if (r == readings.end()) {
            why = "Unable to find an actuator named " + name;
            return false;
        }
        value = r->second;
        return true;
    }
    bool actuateText(const std::string& name, const std::string& value, std::string&) override {
        actuated.push_back(name + "=" + value);
        return true;
    }
    bool moveActuator(const std::string& name, const JPLocation& at, bool withZ, double speed, std::string&) override {
        char text[160];
        std::snprintf(text, sizeof text, "%s to %.2f,%.2f%s at %.2f", name.c_str(), at.x(), at.y(),
                      withZ ? (" z " + std::to_string(int(at.z()))).c_str() : "", speed);
        actuated.push_back(text);
        return true;
    }
    // Where the template is found: `at` less this.
    JPLocation templateOffset { JPLengthUnit::Millimeters };
    int        templateLooks = 0;
    bool matchTemplate(const JPLocation&, const std::string&, const JPTemplateFinder::Area&, JPLocation& offset,
                       std::string&) override {
        ++templateLooks;
        offset = templateOffset;
        return true;
    }
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

    // A drag feeder: the pin over the start, out, down, dragged at the feed
    // speed to the end, the peel off pulsed, backed off, in; at a 2 mm pitch
    // one drag brings two parts, the first picked 2 mm back along the tape.
    {
        JPXmlElement de;
        std::string error;
        const bool ok = JPXmlReader::parse(R"(<feeder class="org.openpnp.machine.reference.feeder.ReferenceDragFeeder" id="D" name="D" enabled="true" part-id="R1" actuator-name="Pin" peel-off-actuator-name="Peel"><location units="Millimeters" x="50.0" y="10.0" z="-2.0" rotation="0.0"/><feed-start-location units="Millimeters" x="40.0" y="10.0" z="-3.0" rotation="0.0"/><feed-end-location units="Millimeters" x="36.0" y="10.0" z="-3.0" rotation="0.0"/><part-pitch value="2.0" units="Millimeters"/><feed-speed>0.5</feed-speed><vision enabled="false"><area-of-interest x="0" y="0" width="0" height="0"/></vision><backoff-distance value="1.0" units="Millimeters"/></feeder>)", de, error);
        assert(ok);
        config.addFeeder(JPFeeder::fromXml(de));
        machine.actuated.clear();
        assert(JPFeederFeed::feed(config, "D", "N1", machine, nullptr, why, empty));
        const std::vector<std::string> drag = { "Pin to 40.00,10.00 at 1.00", "Pin=1", "Pin to 40.00,10.00 z -3 at 1.00",
                                                "Pin to 36.00,10.00 z -3 at 0.50", "Peel=1", "Peel=0",
                                                "Pin to 37.00,10.00 z -3 at 0.50", "Pin=0" };
        assert(machine.actuated == drag);
        auto at = config.feeder("D")->pickLocation();
        assert(at && std::abs(at->x() - 48) < 1e-9 && at->y() == 10);
        // The second part: no drag, picked at the location.
        machine.actuated.clear();
        assert(JPFeederFeed::feed(config, "D", "N1", machine, nullptr, why, empty) && machine.actuated.empty());
        assert(config.feeder("D")->pickLocation()->x() == 50);
        // Then a drag again.
        assert(JPFeederFeed::feed(config, "D", "N1", machine, nullptr, why, empty) && machine.actuated.size() == 8);

        // With vision: the template looked at before the first drag (the start
        // moved by its offset) and after it, the pick moved by the last.
        JPFeeder& d = *config.feeder("D");
        d.setAttributeAt("vision", "enabled", "true");
        d.setAttributeAt("vision", "template-image-name", "tmpl_1.png");
        d.setAttributeAt("vision/area-of-interest", "width", "100");
        d.setAttributeAt("vision/area-of-interest", "height", "100");
        d.setLengthOf("part-pitch", JPLength(4, JPLengthUnit::Millimeters));
        d.resetVisionOffsets();
        d.partsFed = 0;
        machine.templateOffset = JPLocation(JPLengthUnit::Millimeters, 0.5, -0.25, 0, 0);
        machine.actuated.clear();
        assert(JPFeederFeed::feed(config, "D", "N1", machine, nullptr, why, empty) && machine.templateLooks == 2);
        assert(machine.actuated.front() == "Pin to 39.50,10.25 at 1.00");
        at = config.feeder("D")->pickLocation();
        assert(std::abs(at->x() - 49.5) < 1e-9 && std::abs(at->y() - 10.25) < 1e-9);
        // No area of interest: said.
        config.feeder("D")->setAttributeAt("vision/area-of-interest", "width", "0");
        config.feeder("D")->resetVisionOffsets();
        assert(!JPFeederFeed::feed(config, "D", "N1", machine, nullptr, why, empty));
        assert(why == "Area of Interest is required when vision is enabled.");
        // No actuator: said.
        config.feeder("D")->setText("actuator-name", "");
        assert(!JPFeederFeed::feed(config, "D", "N1", machine, nullptr, why, empty) && why == "No actuator name set.");
    }

    // A lever feeder: pushed from the start to the end at the feed speed and
    // let back, the take up on meanwhile; once per 4 mm of pitch; at 2 mm,
    // two parts a push, the second's step only with vision.
    {
        JPXmlElement le;
        std::string error;
        const bool ok = JPXmlReader::parse(R"(<feeder class="org.openpnp.machine.reference.feeder.ReferenceLeverFeeder" id="L" name="L" enabled="true" part-id="R1" actuator-name="Lever" peel-off-actuator-name="TakeUp"><location units="Millimeters" x="70.0" y="20.0" z="-2.0" rotation="0.0"/><feed-start-location units="Millimeters" x="60.0" y="20.0" z="-1.0" rotation="0.0"/><feed-end-location units="Millimeters" x="60.0" y="24.0" z="-4.0" rotation="0.0"/><part-pitch value="4.0" units="Millimeters"/><feed-speed>0.25</feed-speed><vision enabled="false"/></feeder>)", le, error);
        assert(ok);
        config.addFeeder(JPFeeder::fromXml(le));
        machine.actuated.clear();
        assert(JPFeederFeed::feed(config, "L", "N1", machine, nullptr, why, empty));
        const std::vector<std::string> push = { "Lever to 60.00,20.00 at 1.00", "Lever=1", "Lever to 60.00,24.00 z -4 at 0.25",
                                                "TakeUp=1", "Lever to 60.00,20.00 at 1.00", "TakeUp=0", "Lever=0" };
        assert(machine.actuated == push);
        assert(config.feeder("L")->pickLocation()->x() == 70);
        // 8 mm: two pushes.
        config.feeder("L")->setLengthOf("part-pitch", JPLength(8, JPLengthUnit::Millimeters));
        machine.actuated.clear();
        assert(JPFeederFeed::feed(config, "L", "N1", machine, nullptr, why, empty) && machine.actuated.size() == 14);
        // 2 mm: one push for two parts; the first's step back only with vision.
        config.feeder("L")->setLengthOf("part-pitch", JPLength(2, JPLengthUnit::Millimeters));
        machine.actuated.clear();
        assert(JPFeederFeed::feed(config, "L", "N1", machine, nullptr, why, empty) && machine.actuated.size() == 7);
        assert(config.feeder("L")->pickLocation()->x() == 70);
        JPFeeder& l = *config.feeder("L");
        l.setAttributeAt("vision", "enabled", "true");
        l.setAttributeAt("vision", "template-image-name", "tmpl_2.png");
        l.setAttributeAt("vision/area-of-interest", "width", "50");
        l.setAttributeAt("vision/area-of-interest", "height", "50");
        l.partsFed = 0;
        machine.templateLooks = 0;
        machine.templateOffset = JPLocation(JPLengthUnit::Millimeters, 0.5, 0, 0, 0);
        machine.actuated.clear();
        // No look before the push (unlike a drag feeder): one, after it.
        assert(JPFeederFeed::feed(config, "L", "N1", machine, nullptr, why, empty) && machine.templateLooks == 1);
        assert(machine.actuated.front() == "Lever to 60.00,20.00 at 1.00");
        assert(std::abs(config.feeder("L")->pickLocation()->x() - 67.5) < 1e-9);
        assert(JPFeederFeed::feed(config, "L", "N1", machine, nullptr, why, empty) && machine.actuated.size() == 7);
        assert(std::abs(config.feeder("L")->pickLocation()->x() - 69.5) < 1e-9);
    }

    // A Rapid feeder: its address and pitch to the RAPIDFEEDER actuator.
    {
        JPXmlElement re;
        std::string error;
        assert(JPXmlReader::parse(R"(<feeder class="org.openpnp.machine.rapidplacer.RapidFeeder" id="R" name="R" enabled="true" part-id="R1" address="FDR07" pitch="8"><location units="Millimeters" x="5.0" y="6.0" z="-1.0" rotation="0.0"/></feeder>)", re, error));
        config.addFeeder(JPFeeder::fromXml(re));
        machine.actuated.clear();
        assert(JPFeederFeed::feed(config, "R", "N1", machine, nullptr, why, empty));
        assert(machine.actuated.size() == 1 && machine.actuated[0] == "RAPIDFEEDER=FDR07 8");
        assert(config.feeder("R")->pickLocation()->x() == 5);
    }

    // A Schultz feeder: the nozzle over the pick place, its pre pick actuated
    // with its feeder number; post pick too; its buttons read and actuate
    // with it, what they read given back.
    {
        JPXmlElement se;
        std::string error;
        assert(JPXmlReader::parse(R"(<feeder class="org.openpnp.machine.reference.feeder.SchultzFeeder" id="Z" name="Z" enabled="true" part-id="R1" actuator-name="Pre" actuator-value="3.0" post-pick-actuator-name="Post" feed-count-actuator-name="Count" clear-count-actuator-name="Clear" pitch-actuator-name="Pitch" toggle-pitch-actuator-name="Toggle" status-actuator-name="Status" id-actuator-name="Id"><location units="Millimeters" x="9.0" y="8.0" z="-1.0" rotation="90.0"/></feeder>)", se, error));
        config.addFeeder(JPFeeder::fromXml(se));
        machine.actuated.clear();
        assert(JPFeederFeed::feed(config, "Z", "N1", machine, nullptr, why, empty));
        assert(machine.actuated.size() == 1 && machine.actuated[0] == "Pre=3");
        assert(JPFeederFeed::postPick(config, "Z", machine, nullptr, why) && machine.actuated.back() == "Post=3");

        machine.readings = { { "Id", "ID42" }, { "Count", "17" }, { "Pitch", "4" }, { "Status", "OK" } };
        JPFeederActions::Outcome outcome;
        auto& readings = outcome.readings;
        assert(JPFeederActions::run(config, "Z", "getId", machine, nullptr, "", outcome, why));
        assert(readings.size() == 1 && readings[0].first == "getId" && readings[0].second == "ID42");
        assert(machine.actuated.back() == "read Id(3)");
        readings.clear();
        // Test post pick: the count read after.
        assert(JPFeederActions::run(config, "Z", "testPostPick", machine, nullptr, "", outcome, why));
        assert(readings.size() == 1 && readings[0].first == "getFeedCount" && readings[0].second == "17");
        readings.clear();
        // Clear: the count shown empty; toggle: the pitch read after.
        assert(JPFeederActions::run(config, "Z", "clearFeedCount", machine, nullptr, "", outcome, why));
        assert(readings.size() == 1 && readings[0].second.empty() && machine.actuated.back() == "Clear=3");
        readings.clear();
        assert(JPFeederActions::run(config, "Z", "togglePitch", machine, nullptr, "", outcome, why));
        assert(readings.size() == 1 && readings[0].first == "getPitch" && readings[0].second == "4");
        // An actuator not there: OpenPnP's words.
        config.feeder("Z")->setText("status-actuator-name", "Gone");
        assert(!JPFeederActions::run(config, "Z", "getStatus", machine, nullptr, "", outcome, why));
        assert(why == "Failed, unable to find an actuator named Gone");
        // None set: nothing done.
        config.feeder("Z")->setText("actuator-name", "");
        machine.actuated.clear();
        assert(JPFeederFeed::feed(config, "Z", "N1", machine, nullptr, why, empty) && machine.actuated.empty());
    }

    // No hole: the strip's end.
    machine.holes = false;
    config.feeder("S")->visionLocation.reset();   // looked up again: the list moved when "A" was added
    assert(!JPFeederFeed::feed(config, "S", "N1", machine, nullptr, why, empty));
    assert(empty && why == "Unable to locate reference hole. End of strip?");
    return 0;
}
