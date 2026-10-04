// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPFeederForms.h"

#include "JPFormBuilder.h"

#include <cmath>
#include <cstdlib>

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

} // namespace

JPSetupProperties::Form JPFeederForms::forFeeder(JPConfiguration& config, const std::string& feederId,
                                                 std::function<void(const std::string&)> warn) {
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
    } else {
        general(add, config, f, false);
        pickLocation(add, f);
    }
    return form;
}

bool JPFeederForms::act(JPConfiguration& config, const std::string& feederId, const std::string& action) {
    JPFeeder* f = config.feeder(feederId);
    if (!f) return false;
    if (action == "resetFeedCount") {
        f->setNumber("feed-count", 0);
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
