// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPFeederForms.h"

#include "JPFormBuilder.h"

#include "common/JPlacerLog.h"
#include "model/JPFeederTape.h"

#include <j/core/Log.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <tuple>

inline namespace jf {

namespace {

using Place = JPSetupProperties::Place;
constexpr JPLengthUnit kMm = JPLengthUnit::Millimeters;
constexpr int kMostCount = 1000000;
// The highest address a Photon feeder can have (OpenPnP's search's).
constexpr int kMostPhotonAddress = 254;
// Where a slot Schultz feeder loaded for the first time is from its slot (OpenPnP's).
constexpr double kNewSlotFeederOffsetXMm = -5, kNewSlotFeederOffsetYMm = -30;

// A length shown and set in millimetres.
double mm(const JPLength& l) { return l.convertToUnits(kMm).value(); }

std::function<JPFeeder&()> finder(JPConfiguration& config, const std::string& id) {
    return [&config, id]() -> JPFeeder& { return *config.feeder(id); };
}

// A location's coordinate, read and set in millimetres (its rotation as it is).
enum class Axis { X, Y, Z, Rotation };
void coordinate(JPFormBuilder& add, std::function<JPFeeder&()> f, const std::string& element, Axis axis,
                const std::string& label) {
    add.number(element + "." + label, label,
               [f, element, axis] {
                   const JPLocation l = f().locationOf(element).convertToUnits(kMm);
                   return axis == Axis::X ? l.x() : axis == Axis::Y ? l.y() : axis == Axis::Z ? l.z() : l.rotation();
               },
               [f, element, axis](double v) {
                   const JPLocation l = f().locationOf(element).convertToUnits(kMm);
                   f().setLocationOf(element, l.derive(axis == Axis::X ? std::optional(v) : std::nullopt,
                                                       axis == Axis::Y ? std::optional(v) : std::nullopt,
                                                       axis == Axis::Z ? std::optional(v) : std::nullopt,
                                                       axis == Axis::Rotation ? std::optional(v) : std::nullopt));
               });
}

void length(JPFormBuilder& add, std::function<JPFeeder&()> f, const std::string& element, const std::string& label,
            double def) {
    add.number(element, label, [f, element, def] { return mm(f().lengthOf(element, JPLength(def, kMm))); },
               [f, element](double v) { f().setLengthOf(element, JPLength(v, kMm)); });
}

void count(JPFormBuilder& add, std::function<JPFeeder&()> f, const std::string& attribute, const std::string& label, int def) {
    add.integer(attribute, label, [f, attribute, def] { return f().number(attribute, def); },
                [f, attribute](int v) { f().setNumber(attribute, v); }, 0, kMostCount);
}

// OpenPnP's General Settings: the part, and the retries.
void general(JPFormBuilder& add, JPConfiguration& config, std::function<JPFeeder&()> f, bool rotationInTape) {
    add.group("General Settings");
    JPFormBuilder::Strings ids;
    for (const auto& p : config.parts()) ids.push_back(p->id);
    add.choice("part", "Part", ids, [f] { return f().partId(); }, [f](const std::string& id) { f().setPartId(id); });
    if (rotationInTape) {
        coordinate(add, f, "location", Axis::Rotation, "Rotation In Tape");
        add.tip("The Rotation in Tape setting must be interpreted relative to the tape's orientation, regardless of "
                "how the feeder/tape is oriented on the machine.\n"
                "1. Look at the neutral upright orientation of the part package/footprint as drawn inside your E-CAD "
                "library.\n"
                "2. Note how pin 1, polarity, cathode etc. are oriented. This is your 0° for the part.\n"
                "3. Look at the tape so that the sprocket holes are at the top. This is your 0° tape orientation (per "
                "EIA-481 industry standard).\n"
                "4. Determine how the part is rotated inside the tape pocket, relative from its upright orientation "
                "in (1). Positive rotation goes counter-clockwise. This is your Rotation in Tape.");
    }
    add.integer("feed-retry-count", "Feed Retry Count", [f] { return f().feedRetryCount(); },
                [f](int v) { f().setFeedRetryCount(v); }, 0, kMostCount);
    add.integer("pick-retry-count", "Pick Retry Count", [f] { return f().pickRetryCount(); },
                [f](int v) { f().setPickRetryCount(v); }, 0, kMostCount);
}

// OpenPnP's Pick Location: X, Y, Z and rotation, with the location buttons.
void pickLocation(JPFormBuilder& add, std::function<JPFeeder&()> f) {
    add.group("Pick Location");
    add.header({ "X", "Y", "Z", "Rotation" });
    add.row("", Place::Location);
    coordinate(add, f, "location", Axis::X, "X");
    coordinate(add, f, "location", Axis::Y, "Y");
    coordinate(add, f, "location", Axis::Z, "Z");
    coordinate(add, f, "location", Axis::Rotation, "Rotation");
    add.end();
}

void stripForm(JPFormBuilder& add, JPConfiguration& config, std::function<JPFeeder&()> f, bool autoSetupRunning) {
    general(add, config, f, true);

    add.group("Tape Settings");
    if (autoSetupRunning) add.button("autoSetupCancel", "Cancel Auto Setup");
    else add.button("autoSetup", "Auto Setup");
    add.row("Part Pitch");
    length(add, f, "part-pitch", "Part Pitch", 4);
    length(add, f, "tape-width", "Tape Width", 8);
    add.end();
    add.row("Feed Count");
    count(add, f, "feed-count", "Feed Count", 0);
    add.button("resetFeedCount", "Reset", "Reset the feed count to zero, and reset all cached hole positions found by vision.");
    add.end();
    add.row("Max Feed Count");
    count(add, f, "max-feed-count", "Max Feed Count", 0);
    add.tip("Max number of parts to feed from this strip.  If set to zero, this setting is ignored.");
    add.button("autoSetMaxFeedCount", "Auto Set MaxFeedCount",
               "Calculate the Max Feed Count using the feeder's hole locations and part pitch");
    add.end();

    add.group("Vision");
    add.flag("vision-enabled", "Use Vision?", [f] { return f().flag("vision-enabled", false); },
             [f](bool on) { f().setFlag("vision-enabled", on); });
    add.row("");
    add.button("editPipeline", "Edit Pipeline...");
    add.button("resetPipeline", "Reset Pipeline");
    add.button("resetVision", "Reset Vision", "Reset all cached hole positions found by vision.");
    add.end();
    length(add, f, "extrapolation-distance", "Extrapolation Distance", 0);
    add.tip("The maximum tape length before checking vision. If zero, every tape hole will be checked.");
    add.row("Parallax Diameter");
    length(add, f, "parallax-diameter", "Parallax Diameter", 0);
    add.number("parallax-angle", "Parallax Angle",
               [f] { return std::strtod(f().childText("parallax-angle", "0").c_str(), nullptr); },
               [f](double v) {
                   char buf[32];
                   std::snprintf(buf, sizeof buf, "%g", v);
                   f().setChildText("parallax-angle", buf);
               });
    add.end();
    add.tip("When the Parallax Diameter is given, the sprocket hole vision will perform its detection from a "
            "parallax camera view-point on the side of the expected location of the sprocket hole. The distance from "
            "camera to hole is half the Parallax Diameter.\nThe typical application is with transparent tape which is "
            "difficult to see with direct vision, but its reflective surface will reflect the bright diffuser of the "
            "camera light.");

    add.group("Locations");
    add.header({ "X", "Y", "Z" });
    add.row("Reference Hole Location", Place::Location);
    coordinate(add, f, "reference-hole-location", Axis::X, "X");
    coordinate(add, f, "reference-hole-location", Axis::Y, "Y");
    coordinate(add, f, "reference-hole-location", Axis::Z, "Z");
    add.end();
    add.tip("The location of the hole on the tape closest to the center of the first part, in the direction of tape "
            "continuation, i.e. subsequent parts.");
    add.row("Next Hole Location", Place::Location);
    coordinate(add, f, "last-hole-location", Axis::X, "X");
    coordinate(add, f, "last-hole-location", Axis::Y, "Y");
    add.skip();   // no Z, its buttons under the reference hole's
    add.end();
    add.tip("The location of another hole after the reference hole. This can be any hole along the tape as long as "
            "it's past the reference hole.");
}

// OpenPnP's ReferenceLoosePartFeederConfigurationWizard: its pipeline.
void looseForm(JPFormBuilder& add, JPConfiguration& config, std::function<JPFeeder&()> f) {
    general(add, config, f, false);
    pickLocation(add, f);
    add.group("Vision");
    add.row("");
    add.button("editPipeline", "Edit Pipeline...");
    add.button("resetPipeline", "Reset Pipeline");
    add.end();
}

// OpenPnP's AdvancedLoosePartFeederConfigurationWizard: its warning first,
// then the feed and training pipelines.
void advancedLooseForm(JPFormBuilder& add, JPConfiguration& config, std::function<JPFeeder&()> f) {
    add.group("");
    add.note("Warning: This feeder is incomplete and experimental. Use at your own risk.");
    general(add, config, f, false);
    pickLocation(add, f);
    add.group("Vision");
    add.row("Feed Pipeline");
    add.button("editPipeline", "Edit...");
    add.button("resetPipeline", "Reset");
    add.end();
    add.row("Training Pipeline");
    add.button("editTrainingPipeline", "Edit...");
    add.button("resetTrainingPipeline", "Reset");
    add.end();
}

void trayForm(JPFormBuilder& add, JPConfiguration& config, std::function<JPFeeder&()> f,
              std::function<void(const std::string&)> warn) {
    general(add, config, f, false);
    pickLocation(add, f);
    // As OpenPnP's saveToModel: an offset of 0 with more than one part that way is kept, and said.
    auto check = [f, warn] {
        const JPLocation o = f().locationOf("offsets");
        if (!warn) return;
        if (o.x() == 0 && f().number("tray-count-x", 1) > 1)
            warn("X offset must be greater than 0 if X tray count is greater than 1 or feed failure will occur.");
        if (o.y() == 0 && f().number("tray-count-y", 1) > 1)
            warn("Y offset must be greater than 0 if Y tray count is greater than 1 or feed failure will occur.");
    };
    add.group("");
    add.header({ "X", "Y" });
    add.row("Offsets");
    for (const Axis a : { Axis::X, Axis::Y })
        add.number(std::string("offsets.") + (a == Axis::X ? "X" : "Y"), a == Axis::X ? "X" : "Y",
                   [f, a] {
                       const JPLocation l = f().locationOf("offsets").convertToUnits(kMm);
                       return a == Axis::X ? l.x() : l.y();
                   },
                   [f, a, check](double v) {
                       const JPLocation l = f().locationOf("offsets").convertToUnits(kMm);
                       f().setLocationOf("offsets", l.derive(a == Axis::X ? std::optional(v) : std::nullopt,
                                                             a == Axis::Y ? std::optional(v) : std::nullopt, std::nullopt,
                                                             std::nullopt));
                       check();
                   });
    add.end();
    add.row("Tray Count");
    for (const char* a : { "tray-count-x", "tray-count-y" })
        add.integer(a, a[11] == 'x' ? "X" : "Y", [f, a] { return f().number(a, 1); },
                    [f, a, check](int v) {
                        f().setNumber(a, v);
                        check();
                    },
                    0, kMostCount);
    add.end();
    add.row("Feed Count");
    count(add, f, "feed-count", "Feed Count", 0);
    add.button("resetFeedCount", "Reset");
    add.end();
}

// OpenPnP's ReferenceAutoFeederConfigurationWizard's Actuators group.
void autoForm(JPFormBuilder& add, JPConfiguration& config, std::function<JPFeeder&()> f, const std::vector<std::string>& actuators) {
    general(add, config, f, false);
    pickLocation(add, f);
    add.group("Actuators");
    add.header({ "Actuator", "Actuator Value" });
    JPFormBuilder::Strings names { "" };
    names.insert(names.end(), actuators.begin(), actuators.end());
    for (const auto& [row, key, test, testLabel] :
         { std::tuple { "Feed", "actuator", "testFeed", "Test feed" },
           std::tuple { "Post Pick", "post-pick-actuator", "testPostPick", "Test post pick" } }) {
        const std::string nameAttr = std::string(key) + "-name", valueAttr = std::string(key) + "-value";
        add.row(row);
        add.choice(nameAttr, std::string(row) + " actuator", names, [f, nameAttr] { return f().text(nameAttr); },
                   [f, nameAttr](const std::string& n) { f().setText(nameAttr, n); });
        add.number(valueAttr, std::string(row) + " value", [f, valueAttr] { return f().real(valueAttr, 0); },
                   [f, valueAttr](double v) { f().setReal(valueAttr, v); });
        add.button(test, testLabel);
        add.end();
    }
    add.flag("move-before-feed", "Move before feed", [f] { return f().flag("move-before-feed", false); },
             [f](bool on) { f().setFlag("move-before-feed", on); });
    add.tip("Move nozzle to pick location before actuating feed actuator");
    add.flag("recycle-support", "Recycle supported", [f] { return f().flag("recycle-support", false); },
             [f](bool on) { f().setFlag("recycle-support", on); });
    add.tip("Support part recycle from part back to feeder");
}

void templateVision(JPFormBuilder& add, std::function<JPFeeder&()> f, const JPFeederForms::Options& options, bool fromMiddle);

// OpenPnP's ReferenceDragFeederConfigurationWizard (and its
// ReferenceLeverFeederConfigurationWizard, the same but for the 0402 note
// and the backoff): what every feeder has, then the pin's settings, where
// it starts and ends, and the template its vision looks for.
void pinForm(JPFormBuilder& add, JPConfiguration& config, std::function<JPFeeder&()> f, const JPFeederForms::Options& options,
             bool lever) {
    using Selecting = JPFeederForms::Options::Selecting;
    general(add, config, f, false);
    pickLocation(add, f);

    add.group("General Settings");
    // OpenPnP's isPart0402: said beside the pitch.
    const JPPart* part = config.part(f().partId());
    const bool part0402 = !lever && part && (part->packageId.find("C0402") != std::string::npos
                                   || part->packageId.find("R0402") != std::string::npos);
    add.row("Part Pitch");
    length(add, f, "part-pitch", "Part Pitch", 4);
    if (part0402) add.text("part0402", "", [] { return std::string("0402 Part DETECTED"); }, nullptr);
    add.end();
    add.number("feed-speed", "Feed Speed %",
               [f] { return std::strtod(f().childText("feed-speed", "1.0").c_str(), nullptr) * 100; },
               [f](double v) {
                   char buf[32];
                   std::snprintf(buf, sizeof buf, "%g", v / 100);
                   f().setChildText("feed-speed", buf);
               }, 1);
    add.row("Actuator Name");
    add.text("actuator-name", "Actuator Name", [f] { return f().text("actuator-name"); },
             [f](const std::string& v) { f().setText("actuator-name", v); });
    add.text("peel-off-actuator-name", "Peel Off Actuator Name", [f] { return f().text("peel-off-actuator-name"); },
             [f](const std::string& v) { f().setText("peel-off-actuator-name", v); });
    add.end();

    add.group("Locations");
    add.header({ "X", "Y", "Z" });
    for (const auto& [label, element] : { std::pair { "Feed Start Location", "feed-start-location" },
                                          std::pair { "Feed End Location", "feed-end-location" } }) {
        add.row(label, Place::Location);
        coordinate(add, f, element, Axis::X, "X");
        coordinate(add, f, element, Axis::Y, "Y");
        coordinate(add, f, element, Axis::Z, "Z");
        add.actuator([f] { return f().text("actuator-name"); });
        add.end();
    }
    if (!lever) length(add, f, "backoff-distance", "Backoff Distance", 0);

    templateVision(add, f, options, false);
}

// The Vision group of a feeder that finds a template image near its pick
// place (OpenPnP's drag, lever and Neoden 4 feeders): Vision Enabled?, the
// template image with its Select and Cancel, the area of interest (for a
// Neoden 4 feeder from the picture's middle, so its X and Y can be less than
// 0) with its own, and Reset vision offsets.
void templateVision(JPFormBuilder& add, std::function<JPFeeder&()> f, const JPFeederForms::Options& options, bool fromMiddle) {
    using Selecting = JPFeederForms::Options::Selecting;
    add.group("Vision");
    add.flag("vision.enabled", "Vision Enabled?", [f] { return f().attributeAt("vision", "enabled") == "true"; },
             [f](bool on) { f().setAttributeAt("vision", "enabled", on ? "true" : "false"); });
    add.image("Template Image", options.templateImage);
    add.row("");
    add.button("selectTemplate", options.selecting == Selecting::Template ? "Confirm" : "Select", "",
               options.selecting != Selecting::AreaOfInterest);
    add.button("cancelTemplate", "Cancel", "", options.selecting == Selecting::Template);
    add.end();
    add.header({ "X", "Y", "Width", "Height" });
    add.row("Area of Interest");
    for (const char* a : { "x", "y", "width", "height" }) {
        const std::string attr = a;
        const bool signedPlace = fromMiddle && (attr == "x" || attr == "y");
        add.integer("vision.area-of-interest." + attr, attr,
                    [f, attr] { return std::atoi(f().attributeAt("vision/area-of-interest", attr, "0").c_str()); },
                    [f, attr](int v) { f().setAttributeAt("vision/area-of-interest", attr, std::to_string(v)); },
                    signedPlace ? -kMostCount : 0, kMostCount);
    }
    add.button("selectAoi", options.selecting == Selecting::AreaOfInterest ? "Confirm" : "Select", "",
               options.selecting != Selecting::Template);
    add.button("cancelAoi", "Cancel", "", options.selecting == Selecting::AreaOfInterest);
    add.end();
    add.button("resetVisionOffsets", "Reset vision offsets");
}

// OpenPnP's Neoden4FeederConfigurationWizard: what every feeder has, its
// pitch and rotation in the tape, its actuator (Actuate), its feed count
// (Reset), and its template vision.
void neoden4Form(JPFormBuilder& add, JPConfiguration& config, std::function<JPFeeder&()> f, const JPFeederForms::Options& options) {
    general(add, config, f, false);
    pickLocation(add, f);
    add.group("Other");
    add.row("Pitch In Tape [mm]");
    length(add, f, "part-pitch-in-tape", "Pitch In Tape [mm]", 4);
    add.integer("part-rotation-in-tape", "Rotation In Tape [deg]",
                [f] { return std::atoi(f().childText("part-rotation-in-tape", "0").c_str()); },
                [f](int v) { f().setChildText("part-rotation-in-tape", std::to_string(v)); }, -kMostCount, kMostCount);
    add.end();
    add.row("Actuator Name");
    add.text("actuator-name", "Actuator Name", [f] { return f().text("actuator-name"); },
             [f](const std::string& v) { f().setText("actuator-name", v); });
    add.button("actuate", "Actuate");
    add.end();
    add.row("Feed Count");
    count(add, f, "feed-count", "Feed Count", 0);
    add.button("resetFeedCount", "Reset");
    add.end();
    templateVision(add, f, options, true);
}

// OpenPnP's SchultzFeederConfigurationWizard: what every feeder has, its
// feeder number, and its actuators, each with its button and what it read.
void schultzActuators(JPFormBuilder& add, std::function<JPFeeder&()> f, const JPFeederForms::Options& options);

void schultzForm(JPFormBuilder& add, JPConfiguration& config, std::function<JPFeeder&()> f, const JPFeederForms::Options& options) {
    general(add, config, f, false);
    pickLocation(add, f);
    schultzActuators(add, f, options);
}

// A Schultz feeder's Actuators group: its feeder number, and its actuators with their buttons.
void schultzActuators(JPFormBuilder& add, std::function<JPFeeder&()> f, const JPFeederForms::Options& options) {
    add.group("Actuators");
    add.number("actuator-value", "Feeder Number:", [f] { return f().real("actuator-value", 0); },
               [f](double v) { f().setReal("actuator-value", v); });
    add.header({ "Actuator" });
    JPFormBuilder::Strings names { "" };
    names.insert(names.end(), options.actuators.begin(), options.actuators.end());
    const auto reading = options.reading;
    struct Row { const char* label; const char* attribute; const char* action; const char* button; bool shows; const char* note; };
    for (const Row& r : { Row { "Get ID", "id-actuator-name", "getId", "Get ID", true, nullptr },
                          Row { "Pre Pick", "actuator-name", "testFeed", "Test pre pick", false, nullptr },
                          Row { "Post Pick", "post-pick-actuator-name", "testPostPick", "Test post pick", false, nullptr },
                          Row { "Get Feed Count", "feed-count-actuator-name", "getFeedCount", "Get feed count", true, nullptr },
                          Row { "Clear Feed Count", "clear-count-actuator-name", "clearFeedCount", "Clear feed count", false, nullptr },
                          Row { "Get Pitch", "pitch-actuator-name", "getPitch", "Get pitch", true, nullptr },
                          Row { "Toggle Pitch", "toggle-pitch-actuator-name", "togglePitch", "Toggle pitch", false,
                                "Toggle between 2 MM and 4 MM" },
                          Row { "Get Status", "status-actuator-name", "getStatus", "Get status", true, nullptr } }) {
        const std::string attribute = r.attribute, action = r.action;
        add.row(r.label);
        add.choice(attribute, r.label, names, [f, attribute] { return f().text(attribute); },
                   [f, attribute](const std::string& n) { f().setText(attribute, n); });
        add.button(action, r.button);
        if (r.shows)
            add.text(action + ".value", "", [reading, action] { return reading ? reading(action) : std::string(); }, nullptr);
        if (r.note) add.text(action + ".note", "", [note = std::string(r.note)] { return note; }, nullptr);
        add.end();
    }
}

// OpenPnP's ReferenceSlotAutoFeederConfigurationWizard and
// SlotSchultzFeederConfigurationWizard: the slot (the bank's feeder loaded in
// it, named; its location; its retries; its bank, named), the loaded
// feeder's offsets from the slot and its part, and the actuators.
void slotForm(JPFormBuilder& add, JPConfiguration& config, const std::string& slotId, std::function<JPFeeder&()> f,
              const JPFeederForms::Options& options) {
    const bool schultz = f().feedsAs() == "SchultzFeeder";
    const std::string kind = f().typeName();
    // The bank and the loaded feeder, found afresh each time.
    auto bank = [&config, f] { return config.slotBankId(f()); };
    auto loaded = [f] { return f().slotLoad ? f().text("feeder-id") : std::string(); };
    auto banks = [&config, kind]() -> JPSlotBanks& { return config.slotBanks(kind); };

    add.group("Slot");
    JPFormBuilder::Named feeders;
    feeders.add("", "");
    for (const JPSlotBanks::Feeder& bf : banks().feeders(bank())) feeders.add(bf.name, bf.id);
    add.row("Feeder");
    add.byName("slot.feeder", "Feeder", feeders, loaded, [&config, slotId](const std::string& id) { config.loadSlot(slotId, id); });
    add.text("slot.feeder.name", "", [banks, bank, loaded] {
                 const auto bf = banks().feeder(bank(), loaded());
                 return bf ? bf->name : std::string();
             },
             [&config, banks, bank, loaded](const std::string& v) {
                 if (loaded().empty()) return;
                 banks().setFeederName(bank(), loaded(), v);
                 config.resolveSlots();
             });
    if (schultz) {
        add.button("loadSlotFeeder", "Load", "Load installed feeder to slot.");
        add.button("deleteSlotFeeder", "Delete", "Remove selected feeder from database.", !loaded().empty());
    } else {
        add.button("newSlotFeeder", "New");
        add.button("deleteSlotFeeder", "Delete", "", !loaded().empty());
    }
    add.end();
    add.header({ "X", "Y", "Z", "Rotation" });
    add.row("Location", Place::Location);
    coordinate(add, f, "location", Axis::X, "X");
    coordinate(add, f, "location", Axis::Y, "Y");
    coordinate(add, f, "location", Axis::Z, "Z");
    coordinate(add, f, "location", Axis::Rotation, "Rotation");
    if (schultz) add.iconButton("updateLocation", "board-fiducial-locate", "Update feeder location based on fiducial");
    add.end();
    if (schultz)
        add.text("fiducial-part", "Fiducial Part", [f] { return f().text("fiducial-part"); },
                 [f](const std::string& v) { f().setText("fiducial-part", v); });
    add.integer("feed-retry-count", "Feed Retry Count", [f] { return f().feedRetryCount(); },
                [f](int v) { f().setFeedRetryCount(v); }, 0, kMostCount);
    add.integer("pick-retry-count", "Pick Retry Count", [f] { return f().pickRetryCount(); },
                [f](int v) { f().setPickRetryCount(v); }, 0, kMostCount);
    add.endColumns();
    JPFormBuilder::Named bankNames;
    for (const JPSlotBanks::Bank& b : banks().banks()) bankNames.add(b.name, b.id);
    add.row("Bank");
    add.byName("slot.bank", "Bank", bankNames, bank, [&config, slotId](const std::string& id) { config.setSlotBank(slotId, id); });
    add.text("slot.bank.name", "", [banks, bank] {
                 for (const JPSlotBanks::Bank& b : banks().banks())
                     if (b.id == bank()) return b.name;
                 return std::string();
             },
             [banks, bank](const std::string& v) { banks().setBankName(bank(), v); });
    add.button("newBank", "New");
    add.button("deleteBank", "Delete");
    add.end();

    add.group("Feeder");
    add.header({ "X", "Y", "Z", "Rotation" });
    add.row("Offsets", Place::Location);
    for (const auto& [axis, label] : { std::pair { Axis::X, "X" }, std::pair { Axis::Y, "Y" }, std::pair { Axis::Z, "Z" },
                                       std::pair { Axis::Rotation, "Rotation" } }) {
        const Axis a = axis;
        add.number(std::string("slot.offsets.") + label, label,
                   [banks, bank, loaded, a] {
                       const auto bf = banks().feeder(bank(), loaded());
                       if (!bf) return 0.0;
                       const JPLocation o = bf->offsets.convertToUnits(kMm);
                       return a == Axis::X ? o.x() : a == Axis::Y ? o.y() : a == Axis::Z ? o.z() : o.rotation();
                   },
                   [&config, banks, bank, loaded, a](double v) {
                       const auto bf = banks().feeder(bank(), loaded());
                       if (!bf) return;
                       const JPLocation o = bf->offsets.convertToUnits(kMm);
                       banks().setFeederOffsets(bank(), bf->id,
                                              o.derive(a == Axis::X ? std::optional(v) : std::nullopt,
                                                       a == Axis::Y ? std::optional(v) : std::nullopt,
                                                       a == Axis::Z ? std::optional(v) : std::nullopt,
                                                       a == Axis::Rotation ? std::optional(v) : std::nullopt));
                       config.resolveSlots();
                   });
    }
    add.base([f] { return std::optional(f().location()); });
    add.end();
    JPFormBuilder::Strings parts { "" };
    for (const auto& p : config.parts()) parts.push_back(p->id);
    add.choice("slot.part", "Part", parts, [banks, bank, loaded] {
                   const auto bf = banks().feeder(bank(), loaded());
                   return bf ? bf->partId : std::string();
               },
               [&config, banks, bank, loaded](const std::string& id) {
                   if (loaded().empty()) return;
                   banks().setFeederPart(bank(), loaded(), id);
                   config.resolveSlots();
               });

    if (schultz) {
        schultzActuators(add, f, options);
        return;
    }
    add.group("Actuators");
    add.header({ "Actuator", "Actuator Value" });
    JPFormBuilder::Strings names { "" };
    names.insert(names.end(), options.actuators.begin(), options.actuators.end());
    for (const auto& [row, key, test, testLabel] :
         { std::tuple { "Feed", "actuator", "testFeed", "Test feed" },
           std::tuple { "Post Pick", "post-pick-actuator", "testPostPick", "Test post pick" } }) {
        const std::string nameAttr = std::string(key) + "-name", valueAttr = std::string(key) + "-value";
        add.row(row);
        add.choice(nameAttr, std::string(row) + " actuator", names, [f, nameAttr] { return f().text(nameAttr); },
                   [f, nameAttr](const std::string& n) { f().setText(nameAttr, n); });
        add.number(valueAttr, std::string(row) + " value", [f, valueAttr] { return f().real(valueAttr, 0); },
                   [f, valueAttr](double v) { f().setReal(valueAttr, v); });
        add.text(valueAttr + ".note", "", [] { return std::string("For Boolean: 1 = True, 0 = False"); }, nullptr);
        add.button(test, testLabel);
        add.end();
    }
    add.flag("move-before-feed", "Move before feed", [f] { return f().flag("move-before-feed", false); },
             [f](bool on) { f().setFlag("move-before-feed", on); });
    add.tip("Move nozzle to pick location before actuating feed actuator");
}

// OpenPnP's Photon feeder property sheets: Feeder (its hardware id and slot
// address with Find; its part, part pitch with Feed and Feed 1mm, retries;
// its slot's location and its part's offset from it, Move While Feeding?),
// shown once it has a hardware id; and Global Config (the search, and the
// slot programming wizard).
void photonForm(JPFormBuilder& add, JPConfiguration& config, std::function<JPFeeder&()> f, const JPFeederForms::Options& options) {
    if (!f().text("hardware-id").empty()) {
        add.tab("Feeder");
        add.group("Info");
        add.text("hardware-id", "Hardware ID: ", [f] { return f().text("hardware-id"); }, nullptr);
        add.row("Slot Address:");
        add.text("photon.slot", "Slot Address:",
                 [f] { return f().photonSlot ? std::to_string(*f().photonSlot) : std::string(); }, nullptr);
        add.button("photonFind", "Find");
        add.end();

        const bool slotted = f().photonSlot.has_value();
        add.group("Part");
        JPFormBuilder::Strings parts { "" };
        for (const auto& p : config.parts()) parts.push_back(p->id);
        add.choice("part", "Part", parts, [f] { return f().text("part-id"); }, [f](const std::string& id) { f().setPartId(id); });
        add.row("Part Pitch");
        count(add, f, "part-pitch", "Part Pitch", 4);
        add.button("photonFeed", "Feed", "", slotted);
        add.button("photonFeed1mm", "Feed 1mm", "", slotted);
        add.end();
        add.integer("feed-retry-count", "Feed Retry Count", [f] { return f().feedRetryCount(); },
                    [f](int v) { f().setFeedRetryCount(v); }, 0, kMostCount);
        add.integer("pick-retry-count", "Pick Retry Count", [f] { return f().pickRetryCount(); },
                    [f](int v) { f().setPickRetryCount(v); }, 0, kMostCount);

        add.group("Location");
        add.header({ "X", "Y", "Z", "Rotation" });
        // The slot's location: the machine's, for whichever feeder is in it.
        auto slotAt = [&config, f]() -> std::optional<JPLocation> {
            return f().photonSlot ? config.photon().slotLocation(*f().photonSlot) : std::nullopt;
        };
        add.row("Slot Location", Place::Location);
        for (const auto& [axis, label] : { std::pair { Axis::X, "X" }, std::pair { Axis::Y, "Y" }, std::pair { Axis::Z, "Z" },
                                           std::pair { Axis::Rotation, "Rotation" } }) {
            const Axis a = axis;
            add.number(std::string("photon.slot.") + label, label,
                       [slotAt, a] {
                           const JPLocation l = slotAt().value_or(JPLocation(kMm)).convertToUnits(kMm);
                           return a == Axis::X ? l.x() : a == Axis::Y ? l.y() : a == Axis::Z ? l.z() : l.rotation();
                       },
                       [&config, f, slotAt, a](double v) {
                           if (!f().photonSlot) return;
                           const JPLocation l = slotAt().value_or(JPLocation(kMm)).convertToUnits(kMm);
                           config.photon().setSlotLocation(*f().photonSlot,
                                                           l.derive(a == Axis::X ? std::optional(v) : std::nullopt,
                                                                    a == Axis::Y ? std::optional(v) : std::nullopt,
                                                                    a == Axis::Z ? std::optional(v) : std::nullopt,
                                                                    a == Axis::Rotation ? std::optional(v) : std::nullopt));
                           config.resolvePhoton();
                       });
        }
        add.end();
        add.row("Part Offset", Place::Location);
        coordinate(add, f, "offset", Axis::X, "X");
        coordinate(add, f, "offset", Axis::Y, "Y");
        coordinate(add, f, "offset", Axis::Z, "Z");
        coordinate(add, f, "offset", Axis::Rotation, "Rotation");
        add.base(slotAt);
        add.end();
        add.flag("move-while-feeding", "Move While Feeding?", [f] { return f().flag("move-while-feeding", true); },
                 [f](bool on) { f().setFlag("move-while-feeding", on); });
        add.tip("move the nozzle above the pick location while feeding the part.");
    }

    add.tab("Global Config");
    add.group("Search");
    const bool searching = options.searchStates && !options.searchStates().empty();
    add.row("Maximum Feeder Address To Scan");
    add.integer("photon.maxFeederAddress", "Maximum Feeder Address To Scan", [&config] { return config.photon().maxFeederAddress(); },
                [&config](int v) { config.photon().setMaxFeederAddress(v); }, 1, kMostPhotonAddress);
    add.button("photonSearch", "Search", "", !searching);
    add.end();
    add.strip(options.searchStates);
    add.group("Program Feeder Slots");
    add.note("If you've built your own slots and need to program them, use this wizard.");
    add.button("photonProgram", "Start Wizard");
}

// OpenPnP's RapidFeederConfigurationWizard: what every feeder has, the
// feeder's address and pitch, and the scan for its QR codes.
void rapidForm(JPFormBuilder& add, JPConfiguration& config, std::function<JPFeeder&()> f) {
    general(add, config, f, false);
    pickLocation(add, f);
    add.group("Rapid Feeder Config");
    add.text("address", "Address", [f] { return f().text("address"); }, [f](const std::string& v) { f().setText("address", v); },
             "long");
    add.integer("pitch", "Pitch", [f] { return f().number("pitch", 4); }, [f](int v) { f().setNumber("pitch", v); }, 0, kMostCount);
    add.group("Rapid Feeder Scanning");
    add.header({ "X", "Y" });
    for (const auto& [label, element] : { std::pair { "Scan Start Location", "scan-start-location" },
                                          std::pair { "Scan End Location", "scan-end-location" } }) {
        add.row(label, Place::Location);
        coordinate(add, f, element, Axis::X, "X");
        coordinate(add, f, element, Axis::Y, "Y");
        add.end();
    }
    length(add, f, "scan-increment", "Scan Increment", 4);
    add.button("scan", "Scan", "Find the feeders' QR codes along the scan: needs a QR code reader, not in jplacer yet.", false);
}

// A length shown as OpenPnP's "%.0f mm" (a pitch's choices).
std::string pitchLabel(const JPLength& l) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.0f mm", mm(l));
    return buf;
}

// OpenPnP's BambooFeederAutoVisionConfigurationWizard: Tape Settings,
// Locations (pick location and two holes), Vision (its pipeline and
// calibration statistics) and the feed and post pick actuators.
void bambooForm(JPFormBuilder& add, JPConfiguration& config, std::function<JPFeeder&()> f, const std::vector<std::string>& actuators) {
    general(add, config, f, false);

    add.group("Tape Settings");
    const JPFormBuilder::Strings pitches { "2 mm", "4 mm", "8 mm", "12 mm", "16 mm", "20 mm", "24 mm", "28 mm", "32 mm" };
    add.row("Part Pitch");
    for (const auto& [element, label] : { std::pair { "part-pitch", "Part Pitch" }, std::pair { "feed-pitch", "Feed Pitch" } })
        add.choice(element, label, pitches, [f, element] { return pitchLabel(f().lengthOf(element, JPLength(4, kMm))); },
                   [f, element](const std::string& v) { f().setLengthOf(element, JPLength(std::strtod(v.c_str(), nullptr), kMm)); });
    add.button("discardParts", "Discard Parts",
               "Discard parts left over in the (multi-part) feed cycle.\nStarts with a fresh feed cycle including vision "
               "calibration (if enabled).");
    add.end();
    add.tip("Pitch of the parts in the tape (2mm, 4mm, 8mm, 12mm, etc.)");
    add.row("Rotation in Tape");
    add.number("rotation-in-feeder", "Rotation in Tape", [f] { return f().real("rotation-in-feeder", 0); },
               [f](double v) { f().setReal("rotation-in-feeder", v); });
    add.integer("feed-count", "Feed Count", [f] { return f().number("feed-count", 0); }, [f](int v) { f().setNumber("feed-count", v); },
                0, kMostCount);
    add.button("resetFeedCount", "Reset Feed Count", "Reset the feed count e.g. when a tape has been changed.");
    add.end();
    add.tip("The Rotation in Tape setting must be interpreted relative to the tape's orientation, regardless of how the "
            "feeder/tape is oriented on the machine.\n"
            "1. Look at the neutral upright orientation of the part package/footprint as drawn inside your E-CAD library.\n"
            "2. Note how pin 1, polarity, cathode etc. are oriented. This is your 0° for the part.\n"
            "3. Look at the tape so that the sprocket holes are at the top. This is your 0° tape orientation (per EIA-481 "
            "industry standard).\n"
            "4. Determine how the part is rotated inside the tape pocket, relative from its upright orientation in (1). "
            "Positive rotation goes counter-clockwise. This is your Rotation in Tape.");

    add.group("Locations");
    add.row("");
    add.button("showVisionFeatures", "Preview Vision Features", "Preview the features recognized by Computer Vision.");
    add.button("autoSetupTape", "Auto-Setup with Camera at Pick Location",
               "Center the camera on the pick location and press this button to Auto-Setup\nIf there are multiple picks per "
               "feed cycle, choose the one closest to the tape reel.");
    add.end();
    add.header({ "X", "Y", "Z" });
    add.row("Pick Location", Place::Location);
    coordinate(add, f, "location", Axis::X, "X");
    coordinate(add, f, "location", Axis::Y, "Y");
    coordinate(add, f, "location", Axis::Z, "Z");
    add.end();
    add.tip("Pick Location of the part. If multiple are produced by a feed operation\nthis must be the last one picked i.e. the "
            "one closest to the the tape reel.");
    for (const auto& [label, element, tip] :
         { std::tuple { "Hole 1 Location", "hole-1-location",
                        "Choose Hole 1 closer to the tape reel.\nIf possible choose two holes that bracket the part(s) to be picked." },
           std::tuple { "Hole 2 Location", "hole-2-location",
                        "Choose Hole 2 further away from the tape reel.\nIf possible choose two holes that bracket the part(s) to be "
                        "picked." } }) {
        add.row(label, Place::Location);
        coordinate(add, f, element, Axis::X, "X");
        coordinate(add, f, element, Axis::Y, "Y");
        add.skip();
        add.end();
        add.tip(tip);
    }
    add.endColumns();
    add.row("Normalize?");
    add.flag("normalize-pick-location", "Normalize?", [f] { return f().flag("normalize-pick-location", true); },
             [f](bool on) { f().setFlag("normalize-pick-location", on); });
    add.flag("snap-to-axis", "Snap to Axis?", [f] { return f().flag("snap-to-axis", false); },
             [f](bool on) { f().setFlag("snap-to-axis", on); });
    add.end();
    add.tip("Normalize the pick location relative to the sprocket holes according to the EIA-481 standard.");

    add.group("Vision");
    add.row("Vision Type");
    add.choice("pipeline-type", "Vision Type", { "ColorKeyed", "CircularSymmetry" },
               [f] { return f().text("pipeline-type", "CircularSymmetry"); },
               [f](const std::string& v) { f().setText("pipeline-type", v); });
    add.button("editPipeline", "Edit Pipeline", "Edit the Pipeline to be used for all vision operations of this feeder.");
    add.button("resetPipeline", "Reset Pipeline", "Reset the Pipeline for this feeder to the selected type default.");
    add.end();
    add.tip("Choose the vision type, then press Reset Pipeline to assign the default pipeline of that type. Sprocket holes are "
            "detected as follows:\n- ColorKeyed: the background under the holes must be of a vivid color (green by default).\n"
            "- CircularSymmetry: the shape of the holes must be circular, their inside/outside must be plain.\n"
            "Both types of pipeline will further assess detected holes by size, alignment, pitch and expected distance.");
    auto shown = [](const JPLength& l) {
        char buf[32];
        std::snprintf(buf, sizeof buf, "%.3f", mm(l));
        return std::string(buf);
    };
    add.row("Calibration Trigger");
    add.choice("calibration-trigger", "Calibration Trigger", { "None", "OnFirstUse", "UntilConfident", "OnEachTapeFeed" },
               [f] { return f().text("calibration-trigger", "UntilConfident"); },
               [f](const std::string& v) { f().setText("calibration-trigger", v); });
    add.text("precision-average", "Precision Average", [f, shown] { return shown(JPFeederTape::precisionAverage(f())); }, nullptr);
    add.text("calibration-count", "Calibration Count", [f] { return std::to_string(f().number("calibration-count", 0)); }, nullptr);
    add.end();
    add.row("Precision wanted");
    length(add, f, "precision-wanted", "Precision wanted", 0.1);
    add.text("precision-confidence-limit", "Precision Confidence Limit",
             [f, shown] { return shown(JPFeederTape::precisionConfidenceLimit(f())); }, nullptr);
    add.button("resetStatistics", "Reset Statistics", "Reset the average obtained precision statistics.");
    add.end();
    add.tip("Precision wanted i.e. the tolerable pick location offset");

    add.group("Actuators");
    add.header({ "Actuator", "Actuator Value" });
    JPFormBuilder::Strings names { "" };
    names.insert(names.end(), actuators.begin(), actuators.end());
    for (const auto& [row, key, test, testLabel, tip] :
         { std::tuple { "Feed", "feed-actuator", "testFeed", "Test feed", "Select the actuator for the feed action" },
           std::tuple { "Post Pick", "post-pick-actuator", "testPostPick", "Test post pick",
                        "Select the actuator for the post pick action\nThis is optional: blank selection will skip the post pick "
                        "operation" } }) {
        const std::string nameAttr = std::string(key) + "-name", valueAttr = std::string(key) + "-value";
        add.row(row);
        add.choice(nameAttr, std::string(row) + " actuator", names, [f, nameAttr] { return f().text(nameAttr); },
                   [f, nameAttr](const std::string& n) { f().setText(nameAttr, n); });
        add.number(valueAttr, std::string(row) + " value", [f, valueAttr] { return f().real(valueAttr, 0); },
                   [f, valueAttr](double v) { f().setReal(valueAttr, v); });
        add.button(test, testLabel, std::string(test) == "testFeed" ? "Do atomic feed, i.e. not full feed based on Part&Feed pitch." : "");
        add.end();
        add.tip(tip);
    }
    add.endColumns();
    add.flag("move-before-feed", "Move before feed", [f] { return f().flag("move-before-feed", false); },
             [f](bool on) { f().setFlag("move-before-feed", on); });
    add.tip("Move nozzle to pick location before actuating the feed actuator");
}

// OpenPnP's ReferencePushPullFeederConfigurationWizard (Locations, Tape
// Settings, Vision) and ReferencePushPullMotionConfigurationWizard (the
// Push-Pull Settings: its actuators and the places the lever is pushed and
// pulled through, with their speeds and delays).
void pushPullForm(JPFormBuilder& add, JPConfiguration& config, std::function<JPFeeder&()> f, const std::vector<std::string>& actuators) {
    general(add, config, f, false);

    add.group("Locations");
    add.row("");
    add.button("showVisionFeatures", "Preview Vision Features", "Preview the features recognized by Computer Vision.");
    add.button("autoSetupTape", "Auto-Setup with Camera at Pick Location",
               "Center the camera on the pick location and press this button to Auto-Setup\nIf there are multiple picks per "
               "feed cycle, choose the one closest to the tape reel.");
    add.end();
    add.header({ "X", "Y", "Z" });
    add.row("Pick Location", Place::Location);
    coordinate(add, f, "location", Axis::X, "X");
    coordinate(add, f, "location", Axis::Y, "Y");
    coordinate(add, f, "location", Axis::Z, "Z");
    add.end();
    add.tip("Pick Location of the part. If multiple are produced by a feed operation\nthis must be the last one picked i.e. the "
            "one closest to the the tape reel.");
    add.flag("normalize-pick-location", "Normalize?", [f] { return f().flag("normalize-pick-location", true); },
             [f](bool on) { f().setFlag("normalize-pick-location", on); });
    add.tip("Normalize the pick location relative to the sprocket holes according to the EIA-481 standard.");
    for (const auto& [label, element, tip] :
         { std::tuple { "Hole 1 Location", "hole-1-location",
                        "Choose Hole 1 closer to the tape reel.\nIf possible choose two holes that bracket the part(s) to be picked." },
           std::tuple { "Hole 2 Location", "hole-2-location",
                        "Choose Hole 2 further away from the tape reel.\nIf possible choose two holes that bracket the part(s) to be "
                        "picked." } }) {
        add.row(label, Place::Location);
        coordinate(add, f, element, Axis::X, "X");
        coordinate(add, f, element, Axis::Y, "Y");
        add.skip();
        add.end();
        add.tip(tip);
    }
    add.endColumns();
    add.flag("snap-to-axis", "Snap to Axis?", [f] { return f().flag("snap-to-axis", true); },
             [f](bool on) { f().setFlag("snap-to-axis", on); });
    add.tip("Snap rows of sprocket holes to the Axis parallel.");

    add.group("Tape Settings");
    add.row("Part Pitch");
    length(add, f, "part-pitch", "Part Pitch", 4);
    add.number("rotation-in-feeder", "Rotation in Tape", [f] { return f().real("rotation-in-feeder", 0); },
               [f](double v) { f().setReal("rotation-in-feeder", v); });
    add.end();
    add.tip("Pitch of the parts in the tape (2mm, 4mm, 8mm, 12mm, etc.)");
    add.row("Feed Pitch");
    length(add, f, "feed-pitch", "Feed Pitch", 4);
    add.integer("feed-multiplier", "Multiplier", [f] { return f().number("feed-multiplier", 1); },
                [f](int v) { f().setNumber("feed-multiplier", v); }, 1, kMostCount);
    add.button("discardParts", "Discard Parts",
               "Discard parts left over in the (multi-part) feed cycle.\nStarts with a fresh feed cycle including vision "
               "calibration (if enabled).");
    add.end();
    add.tip("How much the tape will be advanced by one lever actuation (usually multiples of 4mm)");
    add.row("Feed Count");
    count(add, f, "feed-count", "Feed Count", 0);
    add.button("resetFeedCount", "Reset Feed Count", "Reset the feed count e.g. when a tape has been changed.");
    add.end();
    add.tip("Total feed count of the feeder.");

    add.group("Vision");
    auto shown = [](const JPLength& l) {
        char buf[32];
        std::snprintf(buf, sizeof buf, "%.3f", mm(l));
        return std::string(buf);
    };
    add.row("Calibration Trigger");
    add.choice("calibration-trigger", "Calibration Trigger", { "None", "OnFirstUse", "UntilConfident", "OnEachTapeFeed" },
               [f] { return f().text("calibration-trigger", "UntilConfident"); },
               [f](const std::string& v) { f().setText("calibration-trigger", v); });
    add.text("precision-average", "Precision Average", [f, shown] { return shown(JPFeederTape::precisionAverage(f())); }, nullptr);
    add.text("calibration-count", "Calibration Count", [f] { return std::to_string(f().number("calibration-count", 0)); }, nullptr);
    add.end();
    add.row("Precision wanted");
    length(add, f, "precision-wanted", "Precision wanted", 0.1);
    add.text("precision-confidence-limit", "Precision Confidence Limit",
             [f, shown] { return shown(JPFeederTape::precisionConfidenceLimit(f())); }, nullptr);
    add.button("resetStatistics", "Reset Statistics", "Reset the average obtained precision statistics.");
    add.end();
    add.tip("Precision wanted i.e. the tolerable pick location offset");
    add.row("");
    add.button("editPipeline", "Edit Pipeline", "Edit the Pipeline to be used for all vision operations of this feeder.");
    add.choice("pipeline-type", "Vision Type", { "ColorKeyed", "CircularSymmetry" },
               [f] { return f().text("pipeline-type", "ColorKeyed"); }, [f](const std::string& v) { f().setText("pipeline-type", v); });
    add.button("resetPipeline", "Reset Pipeline", "Reset the Pipeline for this feeder to the selected type default.");
    add.end();
    add.tip("Choose the vision type, then press Reset Pipeline to assign the default pipeline of that type. Sprocket holes are "
            "detected as follows:\n- ColorKeyed: the background under the holes must be of a vivid color (green by default).\n"
            "- CircularSymmetry: the shape of the holes must be circular, their inside/outside must be plain.\n"
            "Both types of pipeline will further assess detected holes by size, alignment, pitch and expected distance.");

    add.tab("Push-Pull Motion");
    add.group("Push-Pull Settings");
    JPFormBuilder::Strings names { "" };
    names.insert(names.end(), actuators.begin(), actuators.end());
    add.choice("actuator-name", "Feed Actuator", names, [f] { return f().text("actuator-name"); },
               [f](const std::string& n) { f().setText("actuator-name", n); });
    add.tip("Actuator for feed signal and for motion");
    add.choice("peel-off-actuator-name", "Auxiliary Actuator", names, [f] { return f().text("peel-off-actuator-name"); },
               [f](const std::string& n) { f().setText("peel-off-actuator-name", n); });
    add.tip("Actuator for auxiliary purpose (e.g. peeling).");
    add.header({ "X", "Y", "Z", "Rotation", "↓", "↑↓", "↑" });
    auto flag = [&add, f](const std::string& attr, const std::string& label, bool def) {
        add.flag(attr, label, [f, attr, def] { return f().flag(attr, def); }, [f, attr](bool on) { f().setFlag(attr, on); });
    };
    add.row("Vision Calibrate?");
    for (const char* axis : { "x", "y" }) {
        const std::string attr = std::string("calibrate-motion-") + axis;
        add.flag(attr, std::string("Vision Calibrate ") + axis, [f, attr] { return f().text(attr, "true") == "true"; },
                 [f, attr](bool on) { f().setText(attr, on ? "true" : "false"); });
    }
    add.skip();
    flag("additive-rotation", "Additive", true);
    add.end();
    add.tip("Apply the offsets obtained from Vision Calibration");
    // A place's row: X, Y, Z, rotation, then its push, multi and pull switches (none: not offered).
    auto place = [&](const char* label, const char* element, const char* push, const char* multi, const char* pull, bool pushDef,
                     bool multiDef, bool pullDef) {
        add.row(label, Place::Location);
        add.actuator([f] { return f().text("actuator-name"); });
        coordinate(add, f, element, Axis::X, "X");
        coordinate(add, f, element, Axis::Y, "Y");
        coordinate(add, f, element, Axis::Z, "Z");
        coordinate(add, f, element, Axis::Rotation, "Rotation");
        for (const auto& [attr, def] : { std::pair { push, pushDef }, std::pair { multi, multiDef }, std::pair { pull, pullDef } }) {
            if (!*attr) {
                add.skip();
                continue;
            }
            flag(attr, attr, def);
        }
        add.end();
    };
    // A step's row between two places, in the places' columns: the delay
    // after the one above (under X), the speeds pushing to the one below
    // (under ↓) and pulling to the one above (under ↑).
    auto step = [&](const char* delay, const char* push, const char* pull, bool pushElement) {
        add.row("Delay");
        add.integer(delay, "Delay", [f, delay] { return f().number(delay, 0); }, [f, delay](int v) { f().setNumber(delay, v); }, 0,
                    kMostCount);
        add.skip();
        add.skip();
        add.text(std::string(push) + ".label", "", [] { return std::string("Speed ↑↓"); }, nullptr);
        if (pushElement)
            add.number(push, "Speed ↓", [f, push] { return std::strtod(f().childText(push, "1").c_str(), nullptr); },
                       [f, push](double v) {
                           char buf[32];
                           std::snprintf(buf, sizeof buf, "%g", v);
                           f().setChildText(push, buf);
                       });
        else
            add.number(push, "Speed ↓", [f, push] { return f().real(push, 1); }, [f, push](double v) { f().setReal(push, v); });
        add.skip();
        add.number(pull, "Speed ↑", [f, pull] { return f().real(pull, 1); }, [f, pull](double v) { f().setReal(pull, v); });
        add.end();
        add.tip("The delay (in milliseconds) after reaching this location");
    };
    place("Start Location", "feed-start-location", "", "included-multi-0", "included-pull-0", false, true, true);
    step("delay-0", "feed-speed-push-1", "feed-speed-pull-0", true);
    place("Mid 1 Location", "feed-mid-1-location", "included-push-1", "included-multi-1", "included-pull-1", false, false, false);
    step("delay-1", "feed-speed-push-2", "feed-speed-pull-1", false);
    place("Mid 2 Location", "feed-mid-2-location", "included-push-2", "included-multi-2", "included-pull-2", false, false, false);
    step("delay-2", "feed-speed-push-3", "feed-speed-pull-2", false);
    place("Mid 3 Location", "feed-mid-3-location", "included-push-3", "included-multi-3", "included-pull-3", false, false, false);
    step("delay-3", "feed-speed-push-end", "feed-speed-pull-3", false);
    place("End Location", "feed-end-location", "included-push-end", "included-multi-end", "", true, true, false);
    add.row("Delay");
    add.integer("delay-4", "Delay", [f] { return f().number("delay-4", 0); }, [f](int v) { f().setNumber("delay-4", v); }, 0, kMostCount);
    add.skip();
    add.skip();
    if (f().flag("additive-rotation", true))
        add.button("resetRotation", "Reset",
                   "Reset the current rotation axis to 0° for subsequent\ncapturing/positioning using the location buttons in additive mode.");
    add.end();
    add.tip("The delay (in milliseconds) after reaching this location");
}

// The drop box a heap feeder uses.
std::string heapBox(JPConfiguration& config, const JPFeeder& f) {
    const std::string id = f.text("drop-box-id");
    if (config.dropBoxes().box(id)) return id;
    const std::vector<JPDropBoxes::Box> all = config.dropBoxes().boxes();
    return all.empty() ? std::string() : all.back().id;
}

// OpenPnP's ReferenceHeapFeederConfigurationWizard: the Heap (its drop box,
// centre, three moves, depths, vacuum, part and pipelines) and the DropBox
// (its centre and bottom, where parts are dropped, its pipeline and dummy part).
void heapForm(JPFormBuilder& add, JPConfiguration& config, std::function<JPFeeder&()> f) {
    auto box = [&config, f] { return heapBox(config, f()); };
    add.group("Heap");
    JPFormBuilder::Named boxes;
    for (const JPDropBoxes::Box& b : config.dropBoxes().boxes()) boxes.add(b.name, b.id);
    add.row("DropBox");
    add.byName("drop-box-id", "DropBox", boxes, box, [f](const std::string& id) { f().setText("drop-box-id", id); });
    add.text("drop-box.name", "Name", [&config, box] {
                 const auto b = config.dropBoxes().box(box());
                 return b ? b->name : std::string();
             },
             [&config, box](const std::string& v) { config.dropBoxes().setName(box(), v); });
    add.button("newDropBox", "New");
    add.button("deleteDropBox", "Delete");
    add.end();
    add.header({ "X", "Y", "Z" });
    add.row("Center (Top)", Place::Location);
    coordinate(add, f, "location", Axis::X, "X");
    coordinate(add, f, "location", Axis::Y, "Y");
    coordinate(add, f, "location", Axis::Z, "Z");
    add.end();
    for (const auto& [label, element] : { std::pair { "Move 1", "way-1" }, std::pair { "Move 2", "way-2" }, std::pair { "Move 3", "way-3" } }) {
        add.row(label, Place::Location);
        coordinate(add, f, element, Axis::X, "X");
        coordinate(add, f, element, Axis::Y, "Y");
        add.skip();
        add.end();
    }
    add.endColumns();
    add.row("Feed Retry Count");
    add.integer("feed-retry-count", "Feed Retry Count", [f] { return f().feedRetryCount(); }, [f](int v) { f().setFeedRetryCount(v); }, 0,
                kMostCount);
    add.number("box-depth", "Depth", [f] { return f().real("box-depth", -25); }, [f](double v) { f().setReal("box-depth", v); });
    add.end();
    add.row("Pick Retry Count");
    add.integer("pick-retry-count", "Pick Retry Count", [f] { return f().pickRetryCount(); }, [f](int v) { f().setPickRetryCount(v); }, 0,
                kMostCount);
    add.number("last-feed-depth", "Last Feed Depth", [f] { return f().real("last-feed-depth", 0); },
               [f](double v) { f().setReal("last-feed-depth", v); });
    add.button("resetLastFeedDepth", "Reset");
    add.end();
    add.row("Max flip attempts");
    count(add, f, "throw-away-drop-box-content-after-failed-feeds", "Max flip attempts", 9);
    add.integer("required-vacuum-difference", "Vacuum Difference", [f] { return f().number("required-vacuum-difference", 150); },
                [f](int v) { f().setNumber("required-vacuum-difference", v); }, 0, kMostCount);
    add.end();
    add.tip("After this numer of feed, mark the parts as disposable. So the next feed is done with new parts.");
    JPFormBuilder::Strings ids;
    for (const auto& p : config.parts()) ids.push_back(p->id);
    add.row("Part");
    add.choice("part", "Part", ids, [f] { return f().partId(); }, [f](const std::string& id) { f().setPartId(id); });
    add.flag("poke-for-parts", "Poke for Parts", [f] { return f().childText("poke-for-parts", "false") == "true"; },
             [f](bool on) { f().setChildText("poke-for-parts", on ? "true" : "false"); });
    add.end();
    add.tip("If enabled the nozzle is lifted for each move inside the heap. Reduces the risk to damage (large) parts, but slower.");
    add.row("Detection Pipeline");
    add.button("editPipeline", "Edit");
    add.button("resetPipeline", "Reset");
    add.end();
    add.row("Template Pipeline");
    add.button("editTrainingPipeline", "Edit");
    add.button("resetTrainingPipeline", "Reset");
    add.button("getSamples", "GetSamples");
    add.end();

    add.group("DropBox");
    auto boxPlace = [&config, box](const std::string& which, Axis axis) -> std::pair<std::function<double()>, std::function<void(double)>> {
        auto get = [&config, box, which] {
            const auto b = config.dropBoxes().box(box());
            const JPLocation l = b ? (which == "center" ? b->centerBottom : b->drop) : JPLocation(kMm);
            return l.convertToUnits(kMm);
        };
        return { [get, axis] {
                    const JPLocation l = get();
                    return axis == Axis::X ? l.x() : axis == Axis::Y ? l.y() : l.z();
                },
                 [&config, box, which, get, axis](double v) {
                     const JPLocation l = get().derive(axis == Axis::X ? std::optional(v) : std::nullopt,
                                                       axis == Axis::Y ? std::optional(v) : std::nullopt,
                                                       axis == Axis::Z ? std::optional(v) : std::nullopt, std::nullopt);
                     if (which == "center") config.dropBoxes().setCenterBottom(box(), l);
                     else config.dropBoxes().setDrop(box(), l);
                 } };
    };
    add.header({ "X", "Y", "Z" });
    for (const auto& [label, which] : { std::pair { "Center Bottom", "center" }, std::pair { "Drop Location", "drop" } }) {
        add.row(label, Place::Location);
        for (const auto& [axis, name] : { std::pair { Axis::X, "X" }, std::pair { Axis::Y, "Y" }, std::pair { Axis::Z, "Z" } }) {
            auto [get, set] = boxPlace(which, axis);
            add.number(std::string("drop-box.") + which + "." + name, name, get, set);
        }
        add.end();
    }
    add.endColumns();
    add.row("Parts Pipeline");
    add.button("editDropBoxPipeline", "Edit");
    add.button("resetDropBoxPipeline", "Reset");
    add.end();
    add.row("Dummy Part");
    add.choice("drop-box.dummy-part", "Dummy Part", ids,
               [&config, box] {
                   const auto b = config.dropBoxes().box(box());
                   return b ? b->dummyPartId : std::string();
               },
               [&config, box](const std::string& id) { config.dropBoxes().setDummyPart(box(), id); });
    add.button("cleanDropBox", "Clean DropBox");
    add.end();
    add.tip("Dummy part for moving unknown parts (e.g. to the trash). Is also used to determine the used nozzle.");
}

// OpenPnP's ReferenceRotatedTrayFeederConfigurationWizard: the three corner
// parts (A, B, C), the tray's counts, steps and turn.
void rotatedTrayForm(JPFormBuilder& add, JPConfiguration& config, std::function<JPFeeder&()> f) {
    general(add, config, f, false);
    add.group("Tray Component Locations");
    add.header({ "X", "Y" });
    for (const auto& [label, element] : { std::pair { "Point A: First Row - First Column", "location" },
                                          std::pair { "Point B: First Row - Last Column", "first-row-last-component-location" },
                                          std::pair { "Point C: Last Row - Last Column", "last-component-location" } }) {
        add.row(label, Place::Location);
        coordinate(add, f, element, Axis::X, "X");
        coordinate(add, f, element, Axis::Y, "Y");
        add.end();
    }
    add.group("Tray Parameters");
    add.row("Number of Tray Rows");
    count(add, f, "tray-count-rows", "Number of Tray Rows", 1);
    count(add, f, "tray-count-cols", "Number of Tray Columns", 1);
    add.end();
    add.row("Feed Count");
    count(add, f, "feed-count", "Feed Count", 0);
    add.button("resetFeedCount", "Reset");
    add.text("remaining", "Components remaining:", [f] {
        const int total = std::max(f().number("tray-count-rows", 1), 1) * std::max(f().number("tray-count-cols", 1), 1);
        return std::to_string(std::max(0, total - f().number("feed-count")));
    }, nullptr);
    add.end();
    add.number("component-rotation-in-tray", "Component Rotation in Tray [°]",
               [f] { return f().real("component-rotation-in-tray", 0); },
               [f](double v) { f().setReal("component-rotation-in-tray", v); });
    add.tip("Rotation of the components relative to the tray's A->B (row) axis");
    coordinate(add, f, "location", Axis::Z, "Z Height");
    add.button("calculateOffsets", "Calculate Offsets & Tray Rotation");
    add.row("Column Offset");
    for (const bool x : { true, false })
        add.number(x ? "offsets.X" : "offsets.Y", x ? "Column Offset" : "Row Offset",
                   [f, x] {
                       const JPLocation l = f().locationOf("offsets").convertToUnits(kMm);
                       return x ? l.x() : l.y();
                   },
                   [f, x](double v) {
                       const JPLocation l = f().locationOf("offsets").convertToUnits(kMm);
                       f().setLocationOf("offsets", l.derive(x ? std::optional(v) : std::nullopt, x ? std::nullopt : std::optional(v),
                                                             std::nullopt, std::nullopt));
                   });
    add.end();
    coordinate(add, f, "location", Axis::Rotation, "Tray Rotation [°]");
    add.tip("Angle of the tray's A->B (row) axis relative to the machine's positive X-axis");
}

} // namespace

JPSetupProperties::Form JPFeederForms::forFeeder(JPConfiguration& config, const std::string& feederId,
                                                 std::function<void(const std::string&)> warn, const Options& options) {
    JPSetupProperties::Form form;
    JPFeeder* feeder = config.feeder(feederId);
    if (!feeder) return form;
    form.title = feeder->name();
    JPFormBuilder add(form);
    const auto f = finder(config, feederId);
    const std::string kind = feeder->typeName();
    // A Photon feeder's pages are its property sheets' (photonForm); the others' one page.
    if (kind != "PhotonFeeder") add.tab("Configuration");
    if (kind == "ReferenceStripFeeder") {
        stripForm(add, config, f, options.autoSetupRunning);
    } else if (kind == "ReferenceTrayFeeder") {
        trayForm(add, config, f, std::move(warn));
    } else if (kind == "ReferenceAutoFeeder") {
        autoForm(add, config, f, options.actuators);
    } else if (kind == "ReferencePushPullFeeder") {
        pushPullForm(add, config, f, options.actuators);
    } else if (kind == "ReferenceHeapFeeder") {
        heapForm(add, config, f);
    } else if (kind == "BambooFeederAutoVision") {
        bambooForm(add, config, f, options.actuators);
    } else if (kind == "ReferenceRotatedTrayFeeder") {
        rotatedTrayForm(add, config, f);
    } else if (feeder->isSlot()) {
        slotForm(add, config, feederId, f, options);
    } else if (kind == "SchultzFeeder") {
        schultzForm(add, config, f, options);
    } else if (kind == "Neoden4Feeder") {
        neoden4Form(add, config, f, options);
    } else if (kind == "PhotonFeeder") {
        photonForm(add, config, f, options);
    } else if (kind == "RapidFeeder") {
        rapidForm(add, config, f);
    } else if (kind == "ReferenceLoosePartFeeder") {
        looseForm(add, config, f);
    } else if (kind == "AdvancedLoosePartFeeder") {
        advancedLooseForm(add, config, f);
    } else if (kind == "ReferenceDragFeeder" || kind == "ReferenceLeverFeeder") {
        pinForm(add, config, f, options, kind == "ReferenceLeverFeeder");
    } else {
        general(add, config, f, false);
        pickLocation(add, f);
    }
    return form;
}

namespace {

// A slot feeder's New, Delete and Load (its bank's feeders) and its bank's New and Delete.
bool slotAct(JPConfiguration& config, JPFeeder& slot, const std::string& action,
             const std::function<std::string(const std::string&)>& reading, std::string& why) {
    JPSlotBanks& banks = config.slotBanks(slot.typeName());
    const std::string slotId = slot.id(), bank = config.slotBankId(slot);
    const std::string loaded = slot.slotLoad ? slot.text("feeder-id") : std::string();
    if (action == "newSlotFeeder") {
        config.loadSlot(slotId, banks.addFeeder(bank));
        return true;
    }
    if (action == "deleteSlotFeeder") {
        if (loaded.empty()) return false;
        banks.removeFeeder(bank, loaded);
        config.loadSlot(slotId, "");
        return true;
    }
    if (action == "loadSlotFeeder") {
        // The feeder its Get ID read: the bank's of that name, else a new one
        // of it at OpenPnP's offsets for a new feeder.
        const std::string name = reading ? reading("getId") : std::string();
        for (const JPSlotBanks::Feeder& bf : banks.feeders(bank))
            if (bf.name == name) {
                config.loadSlot(slotId, bf.id);
                return true;
            }
        JLOGC(JPlacerLog::kUi, JLogLevel::Warn) << "No feeder " << name << " exists in bank, so creating new.";
        const std::string id = banks.addFeeder(bank, name);
        banks.setFeederOffsets(bank, id, JPLocation(kMm, kNewSlotFeederOffsetXMm, kNewSlotFeederOffsetYMm, 0, 0));
        config.loadSlot(slotId, id);
        return true;
    }
    if (action == "newBank") {
        config.setSlotBank(slotId, banks.addBank());
        return true;
    }
    if (!banks.removeBank(bank, why)) return false;
    config.resolveSlots();
    return true;
}

} // namespace

bool JPFeederForms::isMachineAction(const std::string& action) {
    for (const char* a : { "testFeed", "testPostPick", "showVisionFeatures", "autoSetupTape", "cleanDropBox", "getSamples", "resetRotation", "getId", "getFeedCount", "clearFeedCount", "getPitch", "togglePitch",
                           "getStatus", "updateLocation", "actuate", "photonFind", "photonFeed", "photonFeed1mm",
                           "photonSearch" })
        if (action == a) return true;
    return false;
}

std::vector<std::string> JPFeederForms::readsOnShow(const JPFeeder& feeder) {
    if (feeder.feedsAs() == "SchultzFeeder") return { "getId", "getFeedCount", "getPitch", "getStatus" };
    return {};
}

bool JPFeederForms::act(JPConfiguration& config, const std::string& feederId, const std::string& action, std::string& why,
                        const std::function<std::string(const std::string&)>& reading) {
    why.clear();
    JPFeeder* f = config.feeder(feederId);
    if (!f) return false;
    if (f->isSlot() && (action == "newSlotFeeder" || action == "deleteSlotFeeder" || action == "loadSlotFeeder"
                        || action == "newBank" || action == "deleteBank"))
        return slotAct(config, *f, action, reading, why);
    if (action == "resetVisionOffsets") {
        f->resetVisionOffsets();
        return true;
    }
    if (action == "calculateOffsets") {
        // OpenPnP's calculateOffsetsAndRotation from points A, B and C.
        const int cols = f->number("tray-count-cols", 1), rows = f->number("tray-count-rows", 1);
        if (cols < 1 || rows < 1) {
            why = "The tray must have at least one row and one column.";
            return false;
        }
        const JPLocation a = f->location().convertToUnits(kMm), b = f->locationOf("first-row-last-component-location").convertToUnits(kMm),
                         c = f->locationOf("last-component-location").convertToUnits(kMm);
        const double ab = std::hypot(b.x() - a.x(), b.y() - a.y()), bc = std::hypot(c.x() - b.x(), c.y() - b.y());
        char buf[300];
        if (ab > 0 && cols == 1) {
            why = "Points A and B are different which is inconsistent with a single tray column. Either set points A and B "
                  "to be the same or increase the number of columns.";
            return false;
        }
        if (ab == 0 && cols > 1) {
            std::snprintf(buf, sizeof buf, "Points A and B are the same which is inconsistent with %d columns. Either set "
                                           "points A and B to be different or set the number of columns to 1.", cols);
            why = buf;
            return false;
        }
        if (bc > 0 && rows == 1) {
            why = "Points B and C are different which is inconsistent with a single tray row. Either set points B and C to "
                  "be the same or increase the number of rows.";
            return false;
        }
        if (bc == 0 && rows > 1) {
            std::snprintf(buf, sizeof buf, "Points B and C are the same which is inconsistent with %d rows. Either set "
                                           "points B and C to be different or set the number of rows to 1.", rows);
            why = buf;
            return false;
        }
        double colStep = cols > 1 ? ab / (cols - 1) : 0, rowStep = rows > 1 ? bc / (rows - 1) : 0;
        const double rowAngle = std::atan2(b.y() - a.y(), b.x() - a.x()) * 180 / M_PI;
        const double colAngle = std::atan2(c.y() - b.y(), c.x() - b.x()) * 180 / M_PI;
        if (rows > 1 && cols > 1) {
            double check = std::remainder(rowAngle - colAngle, 360.0);
            if (std::abs(check) < 90 - 2.5 || std::abs(check) > 90 + 2.5) {
                std::snprintf(buf, sizeof buf, "Tray angle ABC should be 90 degrees but is %.3f degrees, double check "
                                               "coordinates of points A, B and C.", std::abs(check));
                why = buf;
                return false;
            }
            // Defined the other way round: the rows step the other way.
            if (check < 0) rowStep = -rowStep;
        }
        double rotation = f->location().rotation();
        if (cols > 1) rotation = rowAngle;
        else if (rows > 1) rotation = colAngle + 90;
        f->setLocationOf("offsets", JPLocation(kMm, colStep, rowStep, 0, 0));
        f->setLocation(f->location().derive(std::nullopt, std::nullopt, std::nullopt, rotation));
        return true;
    }
    if (action == "resetFeedCount") {
        f->setNumber("feed-count", 0);
        if (f->typeName() == "ReferenceRotatedTrayFeeder") f->setFlag("legacy-picking-in-progress", false);
        f->visionLocation.reset();   // and what vision found, as OpenPnP's Reset says
        f->visionLocationReference.reset();
        return true;
    }
    if (action == "newDropBox") {
        // As OpenPnP's: a new box, chosen, named "New".
        const std::string id = config.dropBoxes().add();
        config.dropBoxes().setName(id, "New");
        f->setText("drop-box-id", id);
        return true;
    }
    if (action == "deleteDropBox") {
        const std::string box = heapBox(config, *f);
        for (const JPFeeder& other : config.feeders())
            if (other.id() != f->id() && other.typeName() == "ReferenceHeapFeeder" && heapBox(config, other) == box) {
                why = "Can't delete a DropBox that is in use by other feeder.";
                return false;
            }
        if (!config.dropBoxes().remove(box, why)) return false;
        f->setText("drop-box-id", heapBox(config, *f));
        return true;
    }
    if (action == "resetLastFeedDepth") {
        f->setReal("last-feed-depth", 0);
        return true;
    }
    if (action == "discardParts") {
        JPFeederTape::discardParts(*f);
        return true;
    }
    if (action == "resetStatistics") {
        JPFeederTape::resetCalibrationStatistics(*f);
        return true;
    }
    if (action == "resetVision") {
        f->visionLocation.reset();
        f->visionLocationReference.reset();
        return true;
    }
    if (action == "autoSetMaxFeedCount") {
        // As OpenPnP: the holes' distance over the part pitch, and the first.
        const JPLocation a = f->locationOf("reference-hole-location").convertToUnits(kMm);
        const JPLocation b = f->locationOf("last-hole-location").convertToUnits(kMm);
        const double pitch = mm(f->lengthOf("part-pitch", JPLength(4, kMm)));
        if (pitch <= 0) return false;
        f->setNumber("max-feed-count", 1 + int(std::lround(std::hypot(b.x() - a.x(), b.y() - a.y()) / pitch)));
        return true;
    }
    return false;
}

} // inline namespace jf
