// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// A strip feeder's feed with vision, as OpenPnP's: the hole fed looked at
// (the first one too, when picking starts mid-strip), the parts following
// where it was found; with an extrapolation distance, holes in between
// skipped; a hole not found is the strip's end. Loose part feeders: the
// part nearest the camera, looked at three times, picked from on top of it.
// Tests check with assert(); a Release build must not compile it away.
#undef NDEBUG
#include <cassert>

#include "model/JPConfiguration.h"
#include "openpnp/JPXmlReader.h"
#include "tasks/JPFeederActions.h"
#include "setup/JPFeederForms.h"
#include "tasks/JPFeederFeed.h"
#include "tasks/JPPhotonFeeders.h"
#include "tasks/JPPhotonPacket.h"

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <map>

using namespace jf;
namespace fs = std::filesystem;

namespace {

bool near(double a, double b) { return std::abs(a - b) < 1e-6; }

// Finds every hole `shiftX` mm off where it is looked for; or none.
class HoleMachine : public JPJobMachine {
public:
    void setRotationModeOffset(const std::string&, std::optional<double>) override {}   // no parts turned here
    double shiftX = 0.3;
    bool   holes = true;
    int    looks = 0;
    // The head camera's tune each feed named for its looks (useHeadTune).
    std::string tuneKey = "unset";
    std::optional<JJson> tune;
    bool tuneFirst = false;
    void useHeadTune(const std::string& key, const std::optional<JJson>& controls, bool first) override {
        tuneKey = key;
        tune = controls;
        tuneFirst = first;
    }
    std::vector<Nozzle> nozzles() const override { return {}; }
    std::vector<std::pair<std::string, std::string>> tips() const override { return {}; }
    bool cameraReaches(const JPLocation&) const override { return true; }
    TipPush tipPush(const std::string&) const override { return { true, 1.0 }; }
    std::string holdingPart(const std::string&) const override { return {}; }
    std::string chosenNozzle() const override { return {}; }
    std::optional<JPLocation> cameraLocation() const override { return std::nullopt; }
    bool safeZ(std::string&) override { return true; }
    bool changeTip(const std::string&, const std::string&, std::string&) override { return true; }
    bool rotate(const std::string&, double, std::string&) override { return true; }
    // Picks and places: where each was made.
    std::vector<JPLocation> picked, placed;
    bool pick(const std::string&, const JPLocation& at, std::string&) override {
        picked.push_back(at);
        return true;
    }
    bool place(const std::string&, const JPLocation& at, std::string&) override {
        placed.push_back(at);
        return true;
    }
    bool discard(const std::string&, std::string&) override { return true; }
    std::vector<JPLocation> positioned;
    bool positionCamera(const JPLocation&, std::string&) override { return true; }
    // A loose part feeder's looks: what each sees, in turn; where each looked from.
    std::vector<SeenRects> sights;
    std::vector<JPLocation> lookedFrom;
    bool seeCircles(const JPLocation&, JPPipeline&, SeenCircles&, std::string& why) override {
        why = "no camera";
        return false;
    }
    bool lookThrough(const JPLocation&, JPPipeline&, Sight&, std::string& why) override {
        why = "no camera";
        return false;
    }
    bool cameraSight(Sight&, std::string& why) override {
        why = "no camera";
        return false;
    }
    void showOnCamera(const cv::Mat&, int) override {}
    // The QR codes seen at each place looked at, by its X (none: none seen).
    std::map<int, std::vector<QrCode>> qr;
    bool readQrCodes(const JPLocation& at, std::vector<QrCode>& codes, std::string&) override {
        const auto it = qr.find(int(std::lround(at.x())));
        codes = it == qr.end() ? std::vector<QrCode> {} : it->second;
        return true;
    }
    // A nozzle's moves (to, speed, straight); its vacuum read in turn (the last again once used up).
    struct Move {
        std::array<std::optional<double>, 4> to;
        double                               speed = 1;
        bool                                 straight = false;
    };
    std::vector<Move>   moves;
    std::vector<double> vacuum { 0 };
    size_t              vacuumReads = 0;
    bool                pickedHere = false;
    bool moveNozzle(const std::string&, std::array<std::optional<double>, 4> to, double speed, bool safeZFirst, std::string&) override {
        moves.push_back({ to, speed, !safeZFirst });
        return true;
    }
    bool vacuumOn(const std::string&, std::string&) override { return true; }
    bool zeroActuatorRotation(const std::string&, std::string&) override { return true; }
    // An actuator's moves: "name to x,y,z,c at speed" (straight) or "name over …" (from safe Z).
    bool positionActuator(const std::string& name, std::array<std::optional<double>, 4> to, double speed, bool safeZFirst,
                          std::string&) override {
        char text[160];
        std::snprintf(text, sizeof text, "%s %s %.2f,%.2f,%.2f,%.2f at %.2f", name.c_str(), safeZFirst ? "over" : "to",
                      to[0].value_or(NAN), to[1].value_or(NAN), to[2].value_or(NAN), to[3].value_or(NAN), speed);
        actuated.push_back(text);
        return true;
    }
    bool pickHere(const std::string&, std::string&) override {
        pickedHere = true;
        return true;
    }
    bool readVacuum(const std::string&, double& level, std::string&) override {
        level = vacuum[std::min(vacuumReads++, vacuum.size() - 1)];
        return true;
    }
    bool seeRects(const JPLocation& at, JPPipeline&, int, SeenRects& seen, std::string& why) override {
        lookedFrom.push_back(at);
        if (sights.empty()) {
            why = "no camera";
            return false;
        }
        seen = sights.front();
        sights.erase(sights.begin());
        return true;
    }
    bool positionNozzle(const std::string&, const JPLocation& at, std::string&) override {
        positioned.push_back(at);
        return true;
    }
    bool actuate(const std::string& name, double value, std::string&) override {
        actuated.push_back(name + "=" + std::to_string(int(value)));
        return true;
    }
    std::vector<std::string> actuated;
    bool homed = true;
    bool isHomed() const override { return homed; }
    // What a read gives: by actuator name.
    std::map<std::string, std::string> readings;
    // The Photon bus: what the feeders on it answer a packet (TIMEOUT: none).
    std::function<std::string(const std::string&)> photonBus;
    bool readActuator(const std::string& name, const std::string& parameter, std::string& value, std::string& why) override {
        if (name == "PhotonFeederData" && photonBus) {
            value = photonBus(parameter);
            return true;
        }
        actuated.push_back("read " + name + "(" + parameter + ")");
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

    // The first feed: the first hole looked at, found 0.3 mm off; the part follows it. No tune yet this run, its
    // Auto-Tune? on (as to begin with): tuned on its first look.
    assert(JPFeederFeed::feed(config, "S", "N1", machine, nullptr, why, empty) && machine.looks == 1);
    assert(machine.tuneKey == "S" && !machine.tune && machine.tuneFirst);
    auto at = config.feeder("S")->pickLocation();
    assert(at && near(at->x(), 103.8) && near(at->y(), 52));
    // Each feed looks at its hole (no extrapolation distance); tuned this run, at that tune, not tuned again.
    config.feeder("S")->cameraTune = JJson::parse(R"({"exposure":{"auto":false,"value":1687}})");
    assert(JPFeederFeed::feed(config, "S", "N1", machine, nullptr, why, empty) && machine.looks == 2);
    assert(machine.tuneKey == "S" && machine.tune && (*machine.tune)["exposure"]["value"].number() == 1687 && !machine.tuneFirst);
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
    // Reset (a reloaded strip): its tune forgotten, its next first look tunes again.
    {
        std::string w;
        assert(JPFeederForms::act(config, "S", "resetFeedCount", w) && !config.feeder("S")->cameraTune);
        config.feeder("S")->setNumber("feed-count", 1);
    }
    // Its Auto-Tune? off: the camera's own settings, no tuning.
    config.feeder("S")->setFlag("auto-tune", false);
    assert(JPFeederFeed::feed(config, "S", "N1", machine, nullptr, why, empty));
    assert(!machine.tune && !machine.tuneFirst);
    config.feeder("S")->setFlag("auto-tune", true);

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
        assert(JPFeederActions::run(config, "Z", "getId", machine, nullptr, JPVisionConfig {}, outcome, why));
        assert(readings.size() == 1 && readings[0].first == "getId" && readings[0].second == "ID42");
        assert(machine.actuated.back() == "read Id(3)");
        readings.clear();
        // Test post pick: the count read after.
        assert(JPFeederActions::run(config, "Z", "testPostPick", machine, nullptr, JPVisionConfig {}, outcome, why));
        assert(readings.size() == 1 && readings[0].first == "getFeedCount" && readings[0].second == "17");
        readings.clear();
        // Clear: the count shown empty; toggle: the pitch read after.
        assert(JPFeederActions::run(config, "Z", "clearFeedCount", machine, nullptr, JPVisionConfig {}, outcome, why));
        assert(readings.size() == 1 && readings[0].second.empty() && machine.actuated.back() == "Clear=3");
        readings.clear();
        assert(JPFeederActions::run(config, "Z", "togglePitch", machine, nullptr, JPVisionConfig {}, outcome, why));
        assert(readings.size() == 1 && readings[0].first == "getPitch" && readings[0].second == "4");
        // An actuator not there: OpenPnP's words.
        config.feeder("Z")->setText("status-actuator-name", "Gone");
        assert(!JPFeederActions::run(config, "Z", "getStatus", machine, nullptr, JPVisionConfig {}, outcome, why));
        assert(why == "Failed, unable to find an actuator named Gone");
        // None set: nothing done.
        config.feeder("Z")->setText("actuator-name", "");
        machine.actuated.clear();
        assert(JPFeederFeed::feed(config, "Z", "N1", machine, nullptr, why, empty) && machine.actuated.empty());
    }

    // A Neoden 4 feeder: its actuator with its pitch, the count on; turned by
    // its rotation in the tape; with vision, moved by where the template is,
    // a failure to look only logged.
    {
        JPXmlElement ne;
        std::string error;
        assert(JPXmlReader::parse(R"(<feeder class="org.openpnp.machine.neoden4.Neoden4Feeder" id="N4" name="N4" enabled="true" part-id="R1" actuator-name="F1" feed-count="0"><location units="Millimeters" x="30.0" y="40.0" z="-1.0" rotation="0.0"/><part-pitch-in-tape value="2.0" units="Millimeters"/><part-rotation-in-tape>90</part-rotation-in-tape><vision enabled="true" template-image-name="tmpl_3.png"><area-of-interest x="-50" y="-50" width="100" height="100"/></vision></feeder>)", ne, error));
        config.addFeeder(JPFeeder::fromXml(ne));
        machine.actuated.clear();
        machine.templateLooks = 0;
        machine.templateOffset = JPLocation(JPLengthUnit::Millimeters, 0.25, 0.5, 0, 0);
        assert(JPFeederFeed::feed(config, "N4", "N1", machine, nullptr, why, empty));
        assert(machine.actuated.size() == 1 && machine.actuated[0] == "F1=2" && machine.templateLooks == 1);
        auto at = config.feeder("N4")->pickLocation();
        assert(std::abs(at->x() - 29.75) < 1e-9 && std::abs(at->y() - 39.5) < 1e-9 && at->rotation() == 90);
        assert(config.feeder("N4")->number("feed-count") == 1);
        // No area of interest: fed all the same.
        config.feeder("N4")->setAttributeAt("vision/area-of-interest", "width", "0");
        assert(JPFeederFeed::feed(config, "N4", "N1", machine, nullptr, why, empty) && machine.templateLooks == 1);
        // Actuate: its actuator with its pitch.
        JPFeederActions::Outcome actuated;
        assert(JPFeederActions::run(config, "N4", "actuate", machine, nullptr, JPVisionConfig {}, actuated, why));
        assert(machine.actuated.back() == "F1=2");
    }

    // Photon: OpenPnP's packet; then feeders on a simulated bus, found, set
    // up, fed (the nozzle over the pick meanwhile), searched for.
    {
        JPPhotonPacket p;
        p.toAddress = 0x2B;
        p.fromAddress = 0x13;
        p.packetId = 0x47;
        p.payload = { 0x03 };
        assert(p.toByteString() == "2B1347010A03");
        const auto d = JPPhotonPacket::decode("2B1347010A03");
        assert(d && d->toAddress == 0x2B && d->payload.size() == 1 && d->payload[0] == 3);
        assert(!JPPhotonPacket::decode("2B1347010B03") && !JPPhotonPacket::decode("TIMEOUT") && !JPPhotonPacket::decode("01234"));

        // Two feeders: A at address 5, B at 6.
        const std::string idA = "00112233445566778899AABB", idB = "445566778899AABBCCDDEEFF";
        std::map<int, std::string> onBus { { 5, idA }, { 6, idB } };
        std::vector<int> fed;
        bool forgetful = false;   // A answers its next feed as not set up
        machine.photonBus = [&](const std::string& sent) -> std::string {
            const auto in = JPPhotonPacket::decode(sent);
            if (!in || in->payload.empty()) return "TIMEOUT";
            JPPhotonPacket out;
            out.toAddress = 0;
            out.packetId = in->packetId;
            const std::string uuid = in->payload.size() >= 13 ? in->uuid(1) : std::string();
            auto add = [&out](const std::string& id) {
                for (size_t i = 0; i < 12; ++i) out.payload.push_back(uint8_t(std::stoi(id.substr(2 * i, 2), nullptr, 16)));
            };
            switch (in->payload[0]) {
                case 0xC0:   // GetFeederAddress: the feeder of that id answers
                    for (const auto& [address, id] : onBus)
                        if (id == uuid) {
                            out.fromAddress = address;
                            out.payload = { 0 };
                            return out.toByteString();
                        }
                    return "TIMEOUT";
                case 0x01:   // GetFeederId
                case 0x02:   // InitializeFeeder
                {
                    const auto f = onBus.find(in->toAddress);
                    if (f == onBus.end()) return "TIMEOUT";
                    out.fromAddress = f->first;
                    out.payload = { uint8_t(in->payload[0] == 0x02 && uuid != f->second ? 0x01 : 0x00) };
                    add(f->second);
                    return out.toByteString();
                }
                case 0x04:   // MoveFeedForward: 20 ms
                    out.fromAddress = in->toAddress;
                    if (forgetful) {
                        forgetful = false;
                        out.payload = { 0x03 };
                        return out.toByteString();
                    }
                    fed.push_back(in->payload[1]);
                    out.payload = { 0x00, 0x00, 20 };
                    return out.toByteString();
                case 0x06:   // MoveFeedStatus: done
                    out.fromAddress = in->toAddress;
                    out.payload = { 0x00 };
                    return out.toByteString();
                default:
                    return "TIMEOUT";
            }
        };

        JPFeeder made = JPFeeder::create("org.openpnp.machine.photon.PhotonFeeder", "R1");
        assert(made.name() == "Unconfigured PhotonFeeder");
        made.setText("hardware-id", idA);
        made.setText("name", "Reel A");
        made.setLocationOf("offset", JPLocation(JPLengthUnit::Millimeters, 0, 10, 0, 0));
        const std::string a = made.id();
        config.addFeeder(std::move(made));
        config.photon().setSlotLocation(5, JPLocation(JPLengthUnit::Millimeters, 100, 50, -1, 90));
        assert(config.feeder(a)->name() == "Reel A (Slot: None)");
        // Not found yet: not enabled, and no pick.
        assert(!config.feeder(a)->pickLocation());

        machine.positioned.clear();
        assert(JPFeederFeed::feed(config, a, "N1", machine, nullptr, why, empty));
        JPFeeder* fa = config.feeder(a);
        assert(fa->photonSlot == 5 && fa->photonInitialized && fa->name() == "Reel A (Slot: 5)");
        assert(fed.size() == 1 && fed[0] == 40);   // 4 mm, in tenths
        // Its pick: the offset turned with the slot, from it; the nozzle went there.
        const auto at = fa->pickLocation();
        assert(at && std::abs(at->x() - 90) < 1e-9 && std::abs(at->y() - 50) < 1e-9);
        assert(machine.positioned.size() == 1);
        // A name set as shown has its slot taken off.
        fa->setName("Reel A (Slot: 5)");
        assert(fa->text("name") == "Reel A");

        // Forgot it was set up: set up again, fed.
        forgetful = true;
        assert(JPFeederFeed::feed(config, a, "N1", machine, nullptr, why, empty) && fed.size() == 2);

        // A search: B found at 6, a new feeder named by its id.
        std::vector<int> found;
        assert(JPPhotonFeeders::findAll(config, machine, nullptr,
                                        [&found](int address, JPPhotonFeeders::SearchState s) {
                                            if (s == JPPhotonFeeders::SearchState::Found) found.push_back(address);
                                        },
                                        why));
        assert((found == std::vector<int> { 5, 6 }));
        const JPFeeder* fb = config.photonFeeder(idB);
        assert(fb && fb->photonSlot == 6 && fb->text("name") == idB);
        // A gone from the bus: the search takes its address away.
        onBus.erase(5);
        assert(JPPhotonFeeders::findAll(config, machine, nullptr, nullptr, why));
        assert(!config.feeder(a)->photonSlot);
        // Not answering: not fed, and said.
        assert(!JPFeederFeed::feed(config, a, "N1", machine, nullptr, why, empty));
        assert(why == "Failed to feed for an unknown reason. Is the feeder inserted?");
        machine.photonBus = nullptr;
    }

    // No hole: the strip's end.
    machine.holes = false;
    config.feeder("S")->visionLocation.reset();   // looked up again: the list moved when "A" was added
    assert(!JPFeederFeed::feed(config, "S", "N1", machine, nullptr, why, empty));
    assert(empty && why == "Unable to locate reference hole. End of strip?");
    // Loose part feeders: from the location, then from each part found, the
    // part nearest the camera picked; an advanced one's angle the other way
    // round, and a part outside its first view not taken.
    {
        for (const char* kind : { "ReferenceLoosePartFeeder", "AdvancedLoosePartFeeder" }) {
            JPXmlElement le;
            assert(JPXmlReader::parse(std::string(R"(<feeder class="org.openpnp.machine.reference.feeder.)") + kind
                                          + R"(" id="LOOSE" name="Bin" enabled="true" part-id="R1"><location units="Millimeters" x="100.0" y="50.0" z="-20.0" rotation="10.0"/></feeder>)",
                                      le, error));
            config.addFeeder(JPFeeder::fromXml(le));
            HoleMachine m;
            JPJobMachine::SeenRects far, near1, near2;
            far.halfWidthMm = far.halfHeightMm = near1.halfWidthMm = near1.halfHeightMm = near2.halfWidthMm = near2.halfHeightMm = 8;
            // Two parts seen first; the one 1 mm off nearer than the one 3 mm off.
            far.rects = { { 103, 50, 30 }, { 101, 50, 20 } };
            near1.rects = { { 101.1, 50.1, 21 } };
            near2.rects = { { 101.2, 50.2, 22 } };
            m.sights = { far, near1, near2 };
            bool empty = false;
            assert(JPFeederFeed::feed(config, "LOOSE", "N1", m, nullptr, why, empty));
            assert(m.lookedFrom.size() == 3 && near(m.lookedFrom[0].x(), 100) && near(m.lookedFrom[1].x(), 101));
            const auto pick = config.feeder("LOOSE")->pickLocation();
            const bool advanced = std::string(kind) == "AdvancedLoosePartFeeder";
            assert(pick && near(pick->x(), 101.2) && near(pick->y(), 50.2) && near(pick->z(), -20));
            assert(near(pick->rotation(), advanced ? -(22 + 10) : 22 + 10));
            assert(config.feeder("LOOSE")->partHeightAbovePickLocation());
            // Nothing seen: no parts.
            HoleMachine none;
            none.sights = { JPJobMachine::SeenRects {} };
            assert(!JPFeederFeed::feed(config, "LOOSE", "N1", none, nullptr, why, empty) && why == "Feeder Bin: No parts found.");
            // An advanced feeder's part outside its first view: none.
            if (advanced) {
                HoleMachine drift;
                JPJobMachine::SeenRects off;
                off.halfWidthMm = off.halfHeightMm = 8;
                off.rects = { { 120, 50, 0 } };
                drift.sights = { off };
                assert(!JPFeederFeed::feed(config, "LOOSE", "N1", drift, nullptr, why, empty) && why == "Feeder Bin: No parts found.");
            }
            config.removeFeeder("LOOSE");
        }
    }
    // A heap feeder: its drop box empty and nothing the right way up in it,
    // parts fetched (stirred into the heap until the vacuum rises, dropped
    // into the box), then the part found in three looks and picked there,
    // at the box's bottom less the part's height.
    {
        auto part = std::make_shared<JPPart>();
        part->id = "HEAPED";
        part->height = JPLength(1, JPLengthUnit::Millimeters);
        config.addPart(part);
        const std::string box = config.dropBoxes().boxes().front().id;
        config.dropBoxes().setCenterBottom(box, JPLocation(JPLengthUnit::Millimeters, 200, 50, -20, 0));
        config.dropBoxes().setDrop(box, JPLocation(JPLengthUnit::Millimeters, 200, 50, -15, 0));
        JPXmlElement he;
        assert(JPXmlReader::parse(R"(<feeder class="org.openpnp.machine.reference.feeder.ReferenceHeapFeeder" id="HEAP" name="Heap" enabled="true" part-id="HEAPED" required-vacuum-difference="150"><location units="Millimeters" x="100.0" y="50.0" z="-10.0" rotation="0.0"/><way-1 units="Millimeters" x="120.0" y="50.0" z="0.0" rotation="0.0"/></feeder>)",
                                  he, error));
        config.addFeeder(JPFeeder::fromXml(he));
        HoleMachine m;
        JPJobMachine::SeenRects none, r1, r2, r3;
        r1.rects = { { 201, 50, 30 } };
        r2.rects = { { 201.1, 50.1, 31 } };
        r3.rects = { { 201.2, 50.2, 32 } };
        // Cleaning: nothing in the box; no part the right way up; nothing to flip; then, fetched, three looks.
        m.sights = { none, none, none, r1, r2, r3 };
        // The level before (three reads), then four looks without parts, then parts on.
        m.vacuum = { 0, 0, 0, 0, 0, 0, 0, 200 };
        assert(JPFeederFeed::feed(config, "HEAP", "N1", m, nullptr, why, empty));
        const auto pick = config.feeder("HEAP")->pickLocation();
        assert(pick && near(pick->x(), 201.2) && near(pick->y(), 50.2) && near(pick->z(), -19) && near(pick->rotation(), 32));
        // Stirred from a third of a part above the last depth, round the corners a little lower each time, at a quarter speed.
        assert(m.pickedHere);
        bool stirred = false;
        for (const auto& mv : m.moves)
            if (mv.straight && near(mv.speed, 0.25) && near(*mv.to[0], 101.125) && near(*mv.to[1], 48.875)) stirred = near(*mv.to[2], -10 + 1 / 1.5 - 0.1);
        assert(stirred);
        // Out of the heap through its move, and the parts dropped into the box.
        assert(!m.placed.empty() && near(m.placed.back().x(), 200) && near(m.placed.back().z(), -15));
        assert(config.feeder("HEAP")->real("last-feed-depth") < 0.6);
        // The box is the heap's now: its parts are not cleaned out on the next feed.
        assert(config.dropBoxes().lastHeap[box] == "HEAP");
        // Nothing found, and nothing grabbed within three part heights of the last depth: none there.
        HoleMachine bare;
        bare.sights = { none, none, none, none, none, none, none, none, none, none };
        bare.vacuum = { 0 };
        assert(!JPFeederFeed::feed(config, "HEAP", "N1", bare, nullptr, why, empty));
        assert(why == "HeapFeeder Heap: Can not grab parts. No parts found on three times part height.");
        // From the top (no last depth), down to the box's depth: the heap is empty.
        config.feeder("HEAP")->setReal("last-feed-depth", 0);
        config.feeder("HEAP")->setReal("box-depth", -2);
        HoleMachine emptied;
        emptied.sights = bare.sights;
        assert(!JPFeederFeed::feed(config, "HEAP", "N1", emptied, nullptr, why, empty));
        assert(why == "HeapFeeder Heap: Can not grab parts. Heap Empty or VacuumDifference wrong.");
        config.removeFeeder("HEAP");
    }
    // A push-pull feeder, no vision: its lever over the start, then for each
    // of two actuations (multiplier 2) on, pushed to the end, the peel on,
    // pulled back to the start at its speed, both off; then the count on. A
    // repeated feed moves nothing.
    {
        JPXmlElement pe;
        assert(JPXmlReader::parse(R"(<feeder class="org.openpnp.machine.reference.feeder.ReferencePushPullFeeder" id="PP" name="PushPull" enabled="true" part-id="R1" actuator-name="Lever" peel-off-actuator-name="Peel" feed-multiplier="2" feed-speed-pull-0="0.5" calibration-trigger="None" additive-rotation="false"><location units="Millimeters" x="10.0" y="10.0" z="-5.0" rotation="0.0"/><hole-1-location units="Millimeters" x="8.0" y="13.5" z="0.0" rotation="0.0"/><hole-2-location units="Millimeters" x="12.0" y="13.5" z="0.0" rotation="0.0"/><feed-start-location units="Millimeters" x="20.0" y="30.0" z="-3.0" rotation="0.0"/><feed-end-location units="Millimeters" x="24.0" y="30.0" z="-3.0" rotation="0.0"/><part-pitch value="4.0" units="Millimeters"/><feed-pitch value="4.0" units="Millimeters"/></feeder>)",
                                  pe, error));
        config.addFeeder(JPFeeder::fromXml(pe));
        HoleMachine m;
        assert(JPFeederFeed::feed(config, "PP", "N1", m, nullptr, why, empty));
        const std::vector<std::string> pushPull { "Lever over 20.00,30.00,-3.00,0.00 at 1.00",
                                                  "Lever=1", "Lever to 24.00,30.00,-3.00,0.00 at 1.00", "Peel=1",
                                                  "Lever to 20.00,30.00,-3.00,0.00 at 0.50", "Peel=0", "Lever=0",
                                                  "Lever=1", "Lever to 24.00,30.00,-3.00,0.00 at 1.00", "Peel=1",
                                                  "Lever to 20.00,30.00,-3.00,0.00 at 0.50", "Peel=0", "Lever=0" };
        assert(m.actuated == pushPull);
        assert(config.feeder("PP")->number("feed-count") == 1);
        // Two parts a feed: the first a pitch on towards hole 2, the second at the pick location, without a feed.
        const auto first = config.feeder("PP")->pickLocation();
        m.actuated.clear();
        assert(JPFeederFeed::feed(config, "PP", "N1", m, nullptr, why, empty) && m.actuated.empty());
        const auto second = config.feeder("PP")->pickLocation();
        assert(first && second && near(first->x(), 14) && near(second->x(), 10) && near(second->y(), 10));
        // Skip next: nothing moves, the same part again.
        config.feeder("PP")->setFeedOptions(JPFeeder::FeedOptions::SkipNext);
        assert(JPFeederFeed::feed(config, "PP", "N1", m, nullptr, why, empty) && m.actuated.empty());
        assert(config.feeder("PP")->number("feed-count") == 2 && config.feeder("PP")->feedOptions() == JPFeeder::FeedOptions::Normal);
        config.removeFeeder("PP");
    }
    // A Rapid scan: along the line a scan increment at a time, each QR code a
    // feeder (the one named by it, else a new one), placed where it was first seen.
    {
        JPXmlElement re;
        assert(JPXmlReader::parse(R"(<feeder class="org.openpnp.machine.rapidplacer.RapidFeeder" id="RB" name="B" enabled="true" part-id="R1"><location units="Millimeters" x="0.0" y="0.0" z="-3.0" rotation="90.0"/><scan-start-location units="Millimeters" x="0.0" y="0.0" z="0.0" rotation="0.0"/><scan-end-location units="Millimeters" x="8.0" y="0.0" z="0.0" rotation="0.0"/><scan-increment value="4.0" units="Millimeters"/></feeder>)",
                                  re, error));
        config.addFeeder(JPFeeder::fromXml(re));
        HoleMachine m;
        const auto mm = JPLengthUnit::Millimeters;
        m.qr[0] = { { "A", JPLocation(mm, 0.5, 1, 0, 0) } };
        m.qr[4] = { { "A", JPLocation(mm, 4.5, 1, 0, 0) }, { "B", JPLocation(mm, 5, 2, 0, 0) } };
        JPFeederActions::Outcome outcome;
        assert(JPFeederActions::run(config, "RB", "rapidScan", m, nullptr, JPVisionConfig {}, outcome, why));
        const JPFeeder* b = config.feeder("RB");
        assert(near(b->location().x(), 5) && near(b->location().z(), -3) && near(b->location().rotation(), 90) && b->text("address") == "B");
        const JPFeeder* a = nullptr;
        for (const JPFeeder& f : config.feeders())
            if (f.typeName() == "RapidFeeder" && f.name() == "A") a = &f;
        assert(a && near(a->location().x(), 0.5) && a->text("address") == "A" && !a->partId().empty());
        config.removeFeeder(a->id());
        config.removeFeeder("RB");
    }
    return 0;
}
