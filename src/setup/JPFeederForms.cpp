// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPFeederForms.h"

#include "JPFormBuilder.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <tuple>

inline namespace jf {

namespace {

using Place = JPSetupProperties::Place;
constexpr JPLengthUnit kMm = JPLengthUnit::Millimeters;
constexpr int kMostCount = 1000000;

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

void stripForm(JPFormBuilder& add, JPConfiguration& config, std::function<JPFeeder&()> f) {
    general(add, config, f, true);

    add.group("Tape Settings");
    add.button("autoSetup", "Auto Setup",
               "Find the strip's holes with the camera: needs the strip feeder's vision, not in jplacer yet.", false);
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
    add.button("editPipeline", "Edit Pipeline", "The pipeline editor: not in jplacer yet.", false);
    add.button("resetPipeline", "Reset Pipeline", "The default pipeline: not in jplacer yet.", false);
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

// OpenPnP's ReferenceDragFeederConfigurationWizard: what every feeder has,
// then the drag's settings, where the pin goes in and is dragged to, and
// the template its vision looks for.
void dragForm(JPFormBuilder& add, JPConfiguration& config, std::function<JPFeeder&()> f, const JPFeederForms::Options& options) {
    using Selecting = JPFeederForms::Options::Selecting;
    general(add, config, f, false);
    pickLocation(add, f);

    add.group("General Settings");
    // OpenPnP's isPart0402: said beside the pitch.
    const JPPart* part = config.part(f().partId());
    const bool part0402 = part && (part->packageId.find("C0402") != std::string::npos
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
    length(add, f, "backoff-distance", "Backoff Distance", 0);

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
        add.integer("vision.area-of-interest." + attr, attr,
                    [f, attr] { return std::atoi(f().attributeAt("vision/area-of-interest", attr, "0").c_str()); },
                    [f, attr](int v) { f().setAttributeAt("vision/area-of-interest", attr, std::to_string(v)); }, 0, kMostCount);
    }
    add.button("selectAoi", options.selecting == Selecting::AreaOfInterest ? "Confirm" : "Select", "",
               options.selecting != Selecting::Template);
    add.button("cancelAoi", "Cancel", "", options.selecting == Selecting::AreaOfInterest);
    add.end();
    add.button("resetVisionOffsets", "Reset vision offsets");
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
    add.tab("Configuration");
    const auto f = finder(config, feederId);
    const std::string kind = feeder->typeName();
    if (kind == "ReferenceStripFeeder") {
        stripForm(add, config, f);
    } else if (kind == "ReferenceTrayFeeder") {
        trayForm(add, config, f, std::move(warn));
    } else if (kind == "ReferenceAutoFeeder") {
        autoForm(add, config, f, options.actuators);
    } else if (kind == "ReferenceRotatedTrayFeeder") {
        rotatedTrayForm(add, config, f);
    } else if (kind == "ReferenceDragFeeder") {
        dragForm(add, config, f, options);
    } else {
        general(add, config, f, false);
        pickLocation(add, f);
    }
    return form;
}

bool JPFeederForms::act(JPConfiguration& config, const std::string& feederId, const std::string& action, std::string& why) {
    why.clear();
    JPFeeder* f = config.feeder(feederId);
    if (!f) return false;
    if (action == "resetVisionOffsets") {
        f->resetDragVisionOffsets();
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
