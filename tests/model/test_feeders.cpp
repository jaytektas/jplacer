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
#include "model/JPFeederTape.h"
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
        onHole.as<JPStripFeeder>()->referenceHoleToPartLinear = JPLength(0, JPLengthUnit::Millimeters);
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
        tilted.as<JPStripFeeder>()->zAlongStripPercent = 5;
        const auto third = tilted.pickLocation();   // 6 mm along
        assert(third && near(third->x(), 103.5) && near(third->y(), 44) && near(third->z(), -20 + 0.05 * 6));
        tilted.setFeedCount(1);
        const auto first = tilted.pickLocation();   // 2 mm before the reference hole
        assert(first && near(first->z(), -20 - 0.05 * 2));
        tilted.as<JPStripFeeder>()->referenceHole = JPLocation(JPLengthUnit::Millimeters, 100, 50, -20.3, 0);
        assert(near(tilted.pickLocation()->z(), -20.3 - 0.05 * 2));   // lowered by hand: every part as much
        tilted.as<JPStripFeeder>()->lastHole = JPLocation(JPLengthUnit::Millimeters, 100, 46, 0, 0);
        assert(near(tilted.pickLocation()->z(), -20.3 - 0.05 * 2));   // a nearer last hole: the same slope
    }
    // Saved before OpenPnP took the rotation in tape as EIA-481's: brought to
    // it as OpenPnP reads it, its Rotation In Tape 90 less, so it picks as before.
    {
        std::string old = kStrip;
        old.replace(old.find(" standard-eia-481=\"true\""), std::string(" standard-eia-481=\"true\"").size(), "");
        JPFeeder legacy = parse(old.c_str());
        assert(legacy.as<JPStripFeeder>()->standardEia481 && near(legacy.location().rotation(), -90));
        const auto first = legacy.pickLocation();
        assert(first && near(first->rotation(), 0));
    }
    // Skip next feed: the count stays once, then feeds go on as normal.
    strip.setFeedOptions(JPFeeder::FeedOptions::SkipNext);
    assert(strip.feed(why) && strip.feedCount() == 3 && strip.feedOptions() == JPFeeder::FeedOptions::Normal);
    assert(strip.feed(why) && strip.feedCount() == 4);
    // Past its Max Feed Count: empty.
    assert(!strip.feed(why) && why == "Tried to feed part: R1  Feeder Strip empty.");
    // Its parallax angle (OpenPnP's element text) its own; and through jplacer's file.
    assert(strip.as<JPStripFeeder>()->parallaxAngle == 0);
    strip.as<JPStripFeeder>()->parallaxAngle = 45;
    {
        const JPFeeder back = JPFeeder::fromJson(JJson::parse(strip.toJson().dump()));
        const JPStripFeeder* s = back.as<JPStripFeeder>();
        assert(s && s->parallaxAngle == 45 && s->referenceHole.x() == 100 && s->lastHole.y() == 34 && s->maxFeedCount == 4);
        assert(near(s->tapeWidth.value(), 8) && s->standardEia481 && back.feedCount() == strip.feedCount() && s->autoTune);
        assert(back.pickLocation() && strip.pickLocation() && near(back.pickLocation()->x(), strip.pickLocation()->x()));
    }

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
    // A Rapid feeder: its address, pitch and scan its own, from OpenPnP's and through jplacer's file.
    {
        const JPFeeder r = parse(R"(<feeder class="org.openpnp.machine.rapidplacer.RapidFeeder" id="RF" name="RF" enabled="true" part-id="R1" address="FDR07" pitch="8">
   <location units="Millimeters" x="5.0" y="6.0" z="-1.0" rotation="0.0"/>
   <scan-start-location units="Millimeters" x="10.0" y="0.0" z="0.0" rotation="0.0"/>
   <scan-end-location units="Millimeters" x="90.0" y="0.0" z="0.0" rotation="0.0"/>
   <scan-increment value="2.5" units="Millimeters"/>
</feeder>)");
        const JPFeeder back = JPFeeder::fromJson(JJson::parse(r.toJson().dump()));
        const JPRapidFeeder* own = back.as<JPRapidFeeder>();
        assert(own && own->address == "FDR07" && own->pitch == 8 && own->scanEnd.x() == 90 && near(own->scanIncrement.value(), 2.5));
        assert(back.pickLocation() && back.pickLocation()->x() == 5 && back.tape().pitchMm == 8.0);
    }
    // Drag, lever and Neoden 4 feeders: a pin's settings, a drag's backoff, a Neoden 4's pitch and whole-degree
    // rotation in tape, and their template vision their own, from OpenPnP's and through jplacer's file.
    {
        const JPFeeder d = parse(R"(<feeder class="org.openpnp.machine.reference.feeder.ReferenceDragFeeder" id="D" name="D" enabled="true" part-id="R1" actuator-name="Pin" peel-off-actuator-name="Peel">
   <location units="Millimeters" x="50.0" y="10.0" z="-2.0" rotation="0.0"/>
   <feed-start-location units="Millimeters" x="40.0" y="10.0" z="-1.0" rotation="0.0"/>
   <feed-end-location units="Millimeters" x="44.0" y="10.0" z="-1.0" rotation="0.0"/>
   <part-pitch value="2.0" units="Millimeters"/>
   <feed-speed>0.5</feed-speed>
   <backoff-distance value="0.3" units="Millimeters"/>
   <vision enabled="true" template-image-name="tmpl_1.png"><area-of-interest x="1" y="2" width="30" height="40"/></vision>
</feeder>)");
        const JPFeeder back = JPFeeder::fromJson(JJson::parse(d.toJson().dump()));
        const JPDragFeeder* own = back.as<JPDragFeeder>();
        assert(own && back.pinFeeding() == own && own->actuatorName == "Pin" && own->peelOffActuatorName == "Peel");
        assert(own->feedEnd.x() == 44 && near(own->partPitch.value(), 2) && near(own->feedSpeed, 0.5) && near(own->backoffDistance.value(), 0.3));
        assert(own->vision.enabled && own->vision.templateImageName == "tmpl_1.png" && own->vision.areaOfInterest.height == 40);
        assert(back.templateVision() == &own->vision && back.tape().pitchMm == 2.0);
        assert(back.templatePath("/cfg") == "/cfg/" + std::string(JPDragFeeder::kClass) + ".Vision/tmpl_1.png");

        const JPFeeder lever = JPFeeder::fromJson(JJson::parse(JPFeeder::create(JPLeverFeeder::kClass, "R1").toJson().dump()));
        assert(lever.as<JPLeverFeeder>() && lever.pinFeeding() && !lever.as<JPDragFeeder>());

        const JPFeeder n = parse(R"(<feeder class="org.openpnp.machine.neoden4.Neoden4Feeder" id="N" name="N" enabled="true" part-id="R1" actuator-name="F1" feed-count="2">
   <location units="Millimeters" x="30.0" y="40.0" z="-1.0" rotation="0.0"/>
   <part-pitch-in-tape value="2.0" units="Millimeters"/>
   <part-rotation-in-tape>90</part-rotation-in-tape>
   <vision enabled="false"/>
</feeder>)");
        JPFeeder nb = JPFeeder::fromJson(JJson::parse(n.toJson().dump()));
        const JPNeoden4Feeder* nown = nb.as<JPNeoden4Feeder>();
        assert(nown && nown->actuatorName == "F1" && nb.feedCount() == 2 && nown->partRotationInTape == 90);
        assert(nb.pickLocation() && near(nb.pickLocation()->rotation(), 90) && nb.tape().rotationDeg == 90.0);
        // Its rotation in tape kept in whole degrees.
        nb.setTape(JPFeeder::Tape { 44.6, std::nullopt, 4.0 });
        assert(nb.as<JPNeoden4Feeder>()->partRotationInTape == 45 && near(nb.as<JPNeoden4Feeder>()->partPitchInTape.value(), 4));
    }
    // A Photon feeder: its hardware id, whole-mm pitch, offset (none: not set up) and Move While Feeding? its own.
    {
        const JPFeeder p = parse(R"(<feeder class="org.openpnp.machine.photon.PhotonFeeder" id="P" name="P" enabled="true" part-id="R1" hardware-id="ABC" part-pitch="2" move-while-feeding="false">
   <location units="Millimeters" x="0.0" y="0.0" z="0.0" rotation="0.0"/>
   <offset units="Millimeters" x="1.0" y="-2.0" z="0.0" rotation="90.0"/>
</feeder>)");
        const JPFeeder back = JPFeeder::fromJson(JJson::parse(p.toJson().dump()));
        const JPPhotonFeeder* own = back.as<JPPhotonFeeder>();
        assert(own && back.isPhoton() && back.hardwareId() == "ABC" && own->partPitch == 2 && !own->moveWhileFeeding);
        assert(own->offset && own->offset->y() == -2 && own->offset->rotation() == 90 && back.tape().pitchMm == 2.0);
        const JPFeeder fresh = JPFeeder::create(JPPhotonFeeder::kClass, "");
        assert(!fresh.as<JPPhotonFeeder>()->offset && fresh.name() == "Unconfigured PhotonFeeder");
    }
    // A heap feeder: its drop box, depths, moves, flips, vacuum difference, pipelines and poking its own.
    {
        const JPFeeder h = parse(R"(<feeder class="org.openpnp.machine.reference.feeder.ReferenceHeapFeeder" id="H" name="H" enabled="true" part-id="R1" drop-box-id="BOX1" box-depth="-12.5" last-feed-depth="-3.0" throw-away-drop-box-content-after-failed-feeds="4" required-vacuum-difference="90">
   <location units="Millimeters" x="0.0" y="0.0" z="0.0" rotation="0.0"/>
   <way-2 units="Millimeters" x="2.0" y="3.0" z="0.0" rotation="0.0"/>
   <feeder-pipeline><stages/></feeder-pipeline>
   <poke-for-parts>true</poke-for-parts>
</feeder>)");
        const JPFeeder back = JPFeeder::fromJson(JJson::parse(h.toJson().dump()));
        const JPHeapFeeder* own = back.as<JPHeapFeeder>();
        assert(own && own->dropBoxId == "BOX1" && own->boxDepth == -12.5 && own->lastFeedDepth == -3 && own->way2.y() == 3);
        assert(own->throwAwayAfterFailedFeeds == 4 && own->requiredVacuumDifference == 90 && own->pokeForParts);
        assert(back.pipeline("feeder-pipeline") && !back.pipeline("training-pipeline"));
    }
    // A blinds feeder: its holder, tape, cover and OCR settings its own (its Push Z Offset read as OpenPnP keeps it,
    // push-z-offset), from OpenPnP's and through jplacer's file.
    {
        const JPFeeder b = parse(R"(<feeder class="org.openpnp.machine.reference.feeder.BlindsFeeder" id="B" name="B" enabled="true" part-id="R1" normalize="false" vision-enabled="false" feed-count="2" feeder-group-name="LEFT" pocket-count="20" first-pocket="3" last-pocket="18" push-speed="0.2" ocr-action="ChangePart" cover-type="PushCover" cover-actuation="CheckOpen">
   <location units="Millimeters" x="0.0" y="0.0" z="-5.0" rotation="90.0"/>
   <fiducial-1-location units="Millimeters" x="10.0" y="10.0" z="0.0" rotation="0.0"/>
   <tape-length value="80.0" units="Millimeters"/>
   <pocket-pitch value="4.0" units="Millimeters"/>
   <push-z-offset value="0.4" units="Millimeters"/>
   <ocr-margin value="-15.0" units="Millimeters"/>
</feeder>)");
        const JPFeeder back = JPFeeder::fromJson(JJson::parse(b.toJson().dump()));
        const JPBlindsFeeder* own = back.as<JPBlindsFeeder>();
        assert(own && !own->normalize && !own->visionEnabled && back.feedCount() == 2 && own->feederGroupName == "LEFT");
        assert(own->pocketCount == 20 && own->firstPocket == 3 && own->lastPocket == 18 && near(own->pushSpeed, 0.2));
        assert(own->ocrAction == "ChangePart" && own->coverType == "PushCover" && own->coverActuation == "CheckOpen");
        assert(own->fiducial1.x() == 10 && near(own->tapeLengthMm, 80) && near(own->pocketPitchMm, 4));
        assert(near(own->pushZOffsetMm, 0.4) && near(own->ocrMarginMm, -15) && back.tape().pitchMm == 4.0);
    }
    // Push-pull and Bamboo feeders: a vision tape's settings (holes, pitches, rotation, pipeline type, calibration),
    // a push-pull's motion, OCR and template settings, a Bamboo's actuators, from OpenPnP's and through jplacer's file.
    {
        const JPFeeder pp = parse(R"(<feeder class="org.openpnp.machine.reference.feeder.ReferencePushPullFeeder" id="PP" name="PP" enabled="true" part-id="R1" rotation-in-feeder="-90.0" snap-to-axis="false" feed-multiplier="2" feed-speed-push-2="0.5" included-push-1="true" delay-3="40" calibrate-motion-X="false" actuator-name="Lever" ocr-wrong-part-action="ChangePart" calibration-count="2" calibration-trigger="OnEachTapeFeed" used-as-template="true">
   <location units="Millimeters" x="10.0" y="10.0" z="-1.0" rotation="0.0"/>
   <hole-1-location units="Millimeters" x="8.0" y="13.5" z="0.0" rotation="0.0"/>
   <hole-2-location units="Millimeters" x="12.0" y="13.5" z="0.0" rotation="0.0"/>
   <feed-mid-2-location units="Millimeters" x="3.0" y="4.0" z="0.0" rotation="0.0"/>
   <part-pitch value="2.0" units="Millimeters"/>
   <feed-speed-push-1>0.25</feed-speed-push-1>
   <sum-of-errors value="0.4" units="Millimeters"/>
   <ocr-region rectify="true"><upper-right-corner units="Millimeters" x="2.0" y="1.0" z="0.0" rotation="0.0"/></ocr-region>
</feeder>)");
        const JPFeeder back = JPFeeder::fromJson(JJson::parse(pp.toJson().dump()));
        const JPPushPullFeeder* p = back.as<JPPushPullFeeder>();
        assert(p && back.visionTape() == p && back.isVisionTape() && p->rotationInFeeder == -90 && !p->snapToAxis);
        assert(p->feedMultiplier == 2 && near(p->feedSpeedPush2, 0.5) && near(p->feedSpeedPush1, 0.25) && p->includedPush1);
        assert(p->delay3 == 40 && !p->calibrateMotionX && p->calibrateMotionY && p->actuatorName == "Lever" && p->usedAsTemplate);
        assert(p->ocrWrongPartAction == "ChangePart" && p->calibrationCount == 2 && near(p->sumOfErrorsMm, 0.4));
        assert(p->calibrationTrigger == "OnEachTapeFeed" && p->pipelineType == "ColorKeyed" && p->hole2.x() == 12 && p->feedMid2.y() == 4);
        assert(p->ocrRegion && p->ocrRegion->rectify && p->ocrRegion->upperRight.x() == 2 && near(p->partPitch.value(), 2));
        assert(back.tape().rotationDeg == -90.0 && back.tape().pitchMm == 2.0 && JPFeederTape::of(back).feedMultiplier == 2);

        const JPFeeder bamboo = parse(R"(<feeder class="org.openpnp.machine.pandaplacer.BambooFeederAutoVision" id="BB" name="BB" enabled="true" part-id="R1" feed-actuator-name="Feed" feed-actuator-value="2.0" move-before-feed="true">
   <location units="Millimeters" x="0.0" y="0.0" z="0.0" rotation="0.0"/>
</feeder>)");
        const JPFeeder bb = JPFeeder::fromJson(JJson::parse(bamboo.toJson().dump()));
        const JPBambooFeeder* b = bb.as<JPBambooFeeder>();
        assert(b && b->feedActuatorName == "Feed" && b->feedActuatorValue == 2 && b->moveBeforeFeed && !b->snapToAxis);
        assert(b->pipelineType == "CircularSymmetry" && JPFeederTape::of(bb).feedMultiplier == 1);
    }
    // A feeder of a class jplacer does not know: kept as read, written back the same.
    {
        const JPFeeder unknown = parse(R"(<feeder class="org.example.SomeFeeder" id="U" name="U" enabled="false" part-id="R1" speed="3"><gadget x="1"/></feeder>)");
        const JJson j = unknown.toJson();
        assert(!unknown.typed() && j["openpnp"]["speed"].number() == 3 && j["openpnp"]["gadget"]["x"].number() == 1);
        const JPFeeder back = JPFeeder::fromJson(JJson::parse(j.dump()));
        assert(!back.typed() && back.toJson().dump() == j.dump() && back.feedCount() == 0 && !back.pickLocation());
    }
    return 0;
}
