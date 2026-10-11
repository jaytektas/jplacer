// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// Feeders as OpenPnP keeps them: a strip's and a tray's pick locations (a strip's part 2 mm from its hole,
// or on it when its Reference Hole To Part Linear says 0) and
// feeds, the feed options, a new feeder, and the feeder a part is taken
// from (the highest priority, then the closest); a feeder through jplacer's feeders.json, what every feeder
// has by name and its kind's own as read.
// Tests check with assert(); a Release build must not compile it away.
#undef NDEBUG
#include <cassert>

#include "model/JPConfiguration.h"
#include "openpnp/JPXmlReader.h"

#include <cmath>
#include <filesystem>

using namespace jf;
namespace fs = std::filesystem;

namespace {

JPFeeder parse(const std::string& xml) {
    JPXmlElement e;
    std::string error;
    const bool ok = JPXmlReader::parse(xml, e, error);
    assert(ok);
    return JPFeeder::fromXml(e);
}

bool near(double a, double b) { return std::abs(a - b) < 1e-6; }

const char* kStrip = R"(<feeder class="org.openpnp.machine.reference.feeder.ReferenceStripFeeder" version="1.1" id="FDR1" name="Strip" enabled="true" part-id="R1" priority="Normal" feed-options="Normal" tape-type="WhitePaper" standard-eia-481="true" vision-enabled="false" feed-count="0" max-feed-count="4">
   <location units="Millimeters" x="0.0" y="0.0" z="0.0" rotation="0.0"/>
   <reference-hole-location units="Millimeters" x="100.0" y="50.0" z="-20.0" rotation="0.0"/>
   <last-hole-location units="Millimeters" x="100.0" y="34.0" z="0.0" rotation="0.0"/>
   <part-pitch value="4.0" units="Millimeters"/>
   <tape-width value="8.0" units="Millimeters"/>
   <parallax-angle>0.0</parallax-angle>
</feeder>)";

const char* kTray = R"(<feeder class="org.openpnp.machine.reference.feeder.ReferenceTrayFeeder" version="1.1" id="FDR2" name="Tray" enabled="true" part-id="U1" priority="Normal" feed-options="Normal" tray-count-x="3" tray-count-y="2" feed-count="0">
   <location units="Millimeters" x="10.0" y="20.0" z="-5.0" rotation="0.0"/>
   <offsets units="Millimeters" x="5.0" y="7.0" z="0.0" rotation="0.0"/>
</feeder>)";

} // namespace

int main() {
    // A strip: the first part across the tape from the reference hole, the
    // next ones a part pitch further towards the next hole.
    JPFeeder strip = parse(kStrip);
    assert(strip.typeName() == "ReferenceStripFeeder" && strip.supportsFeedOptions());
    auto at = strip.pickLocation();
    assert(at && near(at->x(), 103.5) && near(at->y(), 52) && near(at->z(), -20) && near(at->rotation(), 90));
    // The first part on a hole (2 mm pitch tape, as auto setup finds it): no way along from it.
    {
        JPFeeder onHole = parse(kStrip);
        onHole.setLengthOf("reference-hole-to-part-linear", JPLength(0, JPLengthUnit::Millimeters));
        const auto first = onHole.pickLocation();
        assert(first && near(first->x(), 103.5) && near(first->y(), 50));
    }
    std::string why;
    assert(strip.feed(why) && strip.feedCount() == 1);
    assert(strip.feed(why) && strip.feed(why) && strip.feedCount() == 3);
    at = strip.pickLocation();
    assert(at && near(at->x(), 103.5) && near(at->y(), 44));
    // Not level (jplacer's Z Along Strip %: the tape rising 5%, 0.8 mm over the 16 mm to the last hole): each part
    // picked that much higher for how far along from the reference hole it is; the reference hole's Z still the base,
    // and where the last hole is only its direction.
    {
        JPFeeder tilted = strip;
        tilted.setReal("z-along-strip-percent", 5);
        const auto third = tilted.pickLocation();   // 6 mm along
        assert(third && near(third->x(), 103.5) && near(third->y(), 44) && near(third->z(), -20 + 0.05 * 6));
        tilted.setFeedCount(1);
        const auto first = tilted.pickLocation();   // 2 mm before the reference hole
        assert(first && near(first->z(), -20 - 0.05 * 2));
        tilted.setLocationOf("reference-hole-location", JPLocation(JPLengthUnit::Millimeters, 100, 50, -20.3, 0));
        assert(near(tilted.pickLocation()->z(), -20.3 - 0.05 * 2));   // lowered by hand: every part as much
        tilted.setLocationOf("last-hole-location", JPLocation(JPLengthUnit::Millimeters, 100, 46, 0, 0));
        assert(near(tilted.pickLocation()->z(), -20.3 - 0.05 * 2));   // a nearer last hole: the same slope
    }
    // Saved before OpenPnP took the rotation in tape as EIA-481's: brought to
    // it as OpenPnP reads it, its Rotation In Tape 90 less, so it picks as before.
    {
        std::string old = kStrip;
        old.replace(old.find(" standard-eia-481=\"true\""), std::string(" standard-eia-481=\"true\"").size(), "");
        JPFeeder legacy = parse(old.c_str());
        assert(legacy.flag("standard-eia-481", false) && near(legacy.location().rotation(), -90));
        const auto first = legacy.pickLocation();
        assert(first && near(first->rotation(), 0));
    }
    // Skip next feed: the count stays once, then feeds go on as normal.
    strip.setFeedOptions(JPFeeder::FeedOptions::SkipNext);
    assert(strip.feed(why) && strip.feedCount() == 3 && strip.feedOptions() == JPFeeder::FeedOptions::Normal);
    assert(strip.feed(why) && strip.feedCount() == 4);
    // Past its Max Feed Count: empty.
    assert(!strip.feed(why) && why == "Tried to feed part: R1  Feeder Strip empty.");
    // A child's text kept, and written back as it was.
    assert(strip.childText("parallax-angle") == "0.0");
    strip.setChildText("parallax-angle", "45");
    assert(strip.childText("parallax-angle") == "45");

    // A tray: Y first while it is no longer than X, then the next column.
    JPFeeder tray = parse(kTray);
    at = tray.pickLocation();
    assert(at && near(at->x(), 10) && near(at->y(), 20) && near(at->z(), -5));
    assert(tray.feed(why) && tray.feed(why));
    at = tray.pickLocation();
    assert(at && near(at->x(), 10) && near(at->y(), 27));
    assert(tray.feed(why));
    at = tray.pickLocation();
    assert(at && near(at->x(), 15) && near(at->y(), 20));
    for (int i = 0; i < 3; ++i) assert(tray.feed(why));
    assert(!tray.feed(why) && why == "Feeder: Tray (U1) - tray empty.");

    // A rotated tray: along a row, then the next row (the tray's -Y), turned with the tray.
    JPFeeder rot = parse(R"(<feeder class="org.openpnp.machine.reference.feeder.ReferenceRotatedTrayFeeder" id="RT" name="RT" enabled="true" part-id="U2" tray-count-cols="3" tray-count-rows="2" feed-count="0" component-rotation-in-tray="90">
   <location units="Millimeters" x="10.0" y="20.0" z="-3.0" rotation="90.0"/>
   <offsets units="Millimeters" x="5.0" y="4.0" z="0.0" rotation="0.0"/>
</feeder>)");
    assert(rot.feed(why) && rot.feed(why) && rot.feed(why) && rot.feed(why));   // the fourth: row 1, column 0
    at = rot.pickLocation();
    // Column 0, row 1: (0, -4) turned 90 degrees is (4, 0).
    assert(at && near(at->x(), 14) && near(at->y(), 20) && near(at->z(), -3) && near(at->rotation(), 180));
    assert(rot.feed(why) && rot.feed(why) && !rot.feed(why));
    // Through jplacer's file; its offsets and turn from points A, B and C (a 3 x 2 tray at 30 degrees).
    {
        const JPFeeder back = JPFeeder::fromJson(JJson::parse(rot.toJson().dump()));
        const JPRotatedTrayFeeder* t = back.as<JPRotatedTrayFeeder>();
        assert(t && t->cols == 3 && t->rows == 2 && t->componentRotation == 90 && t->feedCount == 6 && t->offsets.y() == 4);
        JPRotatedTrayFeeder corners = *t;
        const double a = 30 * M_PI / 180;
        const JPLocation A(JPLengthUnit::Millimeters, 10, 20, 0, 0);
        corners.firstRowLast = JPLocation(JPLengthUnit::Millimeters, 10 + 10 * std::cos(a), 20 + 10 * std::sin(a), 0, 0);
        corners.last = corners.firstRowLast.add(JPLocation(JPLengthUnit::Millimeters, 6 * std::sin(a), -6 * std::cos(a), 0, 0));
        double rotation = 0;
        std::string why2;
        assert(corners.calculateOffsets(A, rotation, why2) && near(rotation, 30) && near(corners.offsets.x(), 5));
        assert(near(std::abs(corners.offsets.y()), 6));
        corners.cols = 1;
        assert(!corners.calculateOffsets(A, rotation, why2) && why2.find("single tray column") != std::string::npos);
    }

    // A new feeder, as OpenPnP makes one.
    const JPFeeder made = JPFeeder::create("org.openpnp.machine.reference.feeder.ReferenceTrayFeeder", "C1");
    assert(made.id().rfind("FDR", 0) == 0 && made.name() == "ReferenceTrayFeeder" && !made.enabled());
    assert(made.partId() == "C1" && made.priority() == JPFeeder::Priority::Normal && made.feedRetryCount() == 3);
    assert(JPFeeder::create(made.className(), "C1").id() != made.id());
    assert(JPFeeder::classNames().size() == 19 && JPFeeder::simpleName(JPFeeder::classNames().front()) == "ReferenceStripFeeder");

    // The feeder a part is taken from: enabled, the highest priority, then the closest.
    const fs::path dir = fs::temp_directory_path() / "jplacer-test-feeders";
    fs::remove_all(dir);
    fs::create_directories(dir);
    JPConfiguration config(dir.string());
    JPFeeder near1 = parse(kTray), far1 = parse(kTray), high = parse(kTray), off = parse(kTray);
    near1.setId("near");
    far1.setId("far");
    far1.setLocation(JPLocation(JPLengthUnit::Millimeters, 300, 300, 0, 0));
    high.setId("high");
    high.setLocation(JPLocation(JPLengthUnit::Millimeters, 500, 500, 0, 0));
    off.setId("off");
    off.setEnabled(false);
    off.setPriority(JPFeeder::Priority::High);
    config.addFeeder(far1);
    config.addFeeder(near1);
    config.addFeeder(off);
    const JPLocation camera(JPLengthUnit::Millimeters, 0, 0, 0, 0);
    assert(config.findFeeder("U1", camera)->id() == "near");
    assert(config.findFeeder("U1", std::nullopt)->id() == "far");   // no camera: the first
    config.addFeeder(high).setPriority(JPFeeder::Priority::High);
    assert(config.findFeeder("U1", camera)->id() == "high");
    assert(!config.findFeeder("R1", camera));
    assert(config.feederCount("U1") == 4 && config.hasFeeder("U1"));
    config.removeFeeder("high");
    assert(config.feederCount("U1") == 3 && !config.feeder("high"));
    // Through jplacer's file: what every feeder has, and its kind's own, as they were.
    {
        JPFeeder made = parse(kTray);
        made.setFeedRetryCount(5);
        made.setPickRetryCount(1);
        made.setPriority(JPFeeder::Priority::High);
        made.setFeedOptions(JPFeeder::FeedOptions::SkipNext);
        const JJson j = JJson::parse(made.toJson().dump(2));
        assert(j["part"].str() == "U1" && j["location"]["x"].number() == 10 && j["tray"]["countX"].number() == 3 && !j.contains("openpnp"));
        assert(j["tray"]["offsets"]["x"].number() == 5 && j["tray"]["feedCount"].number() == 0);
        const JPFeeder back = JPFeeder::fromJson(j);
        assert(back.id() == "FDR2" && back.plainName() == "Tray" && back.enabled() && back.partId() == "U1");
        assert(back.feedRetryCount() == 5 && back.pickRetryCount() == 1 && back.priority() == JPFeeder::Priority::High);
        assert(back.feedOptions() == JPFeeder::FeedOptions::SkipNext && back.className() == made.className());
        assert(back.location() == made.location() && back.as<JPTrayFeeder>() && back.as<JPTrayFeeder>()->countY == 2);
        assert(back.as<JPTrayFeeder>()->offsets.x() == 5 && back.pickLocation() && back.pickLocation()->x() == made.pickLocation()->x());
    }
    // A feeder of OpenPnP's version 1.0: its retry count its feed retries, no pick retries (as OpenPnP brings it to 1.1).
    {
        const JPFeeder old = parse(R"(<feeder class="org.openpnp.machine.reference.feeder.ReferenceTubeFeeder" id="T" name="T" enabled="true" part-id="R1" retry-count="5">
   <location units="Millimeters" x="1.0" y="2.0" z="0.0" rotation="0.0"/>
</feeder>)");
        assert(old.feedRetryCount() == 5 && old.pickRetryCount() == 0);
        // A tube: typed, with nothing of its own; picked where it is.
        const JJson j = old.toJson();
        assert(old.as<JPTubeFeeder>() && j["tube"].isObject() && !j.contains("openpnp"));
        const JPFeeder back = JPFeeder::fromJson(JJson::parse(j.dump()));
        assert(back.as<JPTubeFeeder>() && back.pickLocation() && back.pickLocation()->x() == 1);
    }
    // An auto feeder: its actuators, values and options its own, from OpenPnP's and through jplacer's file.
    {
        const JPFeeder a = parse(R"(<feeder class="org.openpnp.machine.reference.feeder.ReferenceAutoFeeder" version="1.1" id="A" name="A" enabled="true" part-id="R1" actuator-name="FEED" actuator-value="2.5" post-pick-actuator-name="DONE" post-pick-actuator-value="1.0" move-before-feed="true" recycle-support="true">
   <location units="Millimeters" x="3.0" y="4.0" z="0.0" rotation="0.0"/>
</feeder>)");
        const JPAutoFeeder* own = a.as<JPAutoFeeder>();
        assert(own && own->actuatorName == "FEED" && own->actuatorValue == 2.5 && own->postPickActuatorName == "DONE");
        assert(own->postPickActuatorValue == 1 && own->moveBeforeFeed && own->recycleSupport && a.canTakeBackPart());
        const JPFeeder back = JPFeeder::fromJson(JJson::parse(a.toJson().dump()));
        const JPAutoFeeder* b = back.as<JPAutoFeeder>();
        assert(b && b->actuatorName == "FEED" && b->actuatorValue == 2.5 && b->postPickActuatorName == "DONE" && b->moveBeforeFeed);
        assert(back.pickLocation() && back.pickLocation()->y() == 4);
    }
    // A Schultz feeder and a slot Schultz feeder: their actuators, feeder number and fiducial part their own; a
    // slot's bank and loaded feeder too. From OpenPnP's, and through jplacer's file.
    {
        const JPFeeder z = parse(R"(<feeder class="org.openpnp.machine.reference.feeder.SlotSchultzFeeder" version="1.1" id="SZ" name="SZ" enabled="true" part-id="" bank-id="B1" feeder-id="F7" actuator-name="Pre" actuator-value="4.0" post-pick-actuator-name="Post" status-actuator-name="Status" id-actuator-name="Id" fiducial-part="FID-1">
   <location units="Millimeters" x="0.0" y="0.0" z="0.0" rotation="0.0"/>
</feeder>)");
        const JPSlotSchultzFeeder* own = z.as<JPSlotSchultzFeeder>();
        assert(own && z.schultzFeeding() == own && own->actuatorName == "Pre" && own->actuatorValue == 4);
        assert(own->statusActuatorName == "Status" && own->idActuatorName == "Id" && own->fiducialPart == "FID-1");
        assert(z.slotBankId() == "B1" && z.slotFeederId() == "F7" && z.isSlot() && !z.autoFeeding());
        const JPFeeder back = JPFeeder::fromJson(JJson::parse(z.toJson().dump()));
        const JPSlotSchultzFeeder* b = back.as<JPSlotSchultzFeeder>();
        assert(b && b->postPickActuatorName == "Post" && b->actuatorValue == 4 && b->fiducialPart == "FID-1");
        assert(back.slotBankId() == "B1" && back.slotFeederId() == "F7");
        JPFeeder plain = JPFeeder::create(JPSchultzFeeder::kClass, "R1");
        assert(plain.as<JPSchultzFeeder>() && plain.slotBankId().empty());
    }
    return 0;
}
