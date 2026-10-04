// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPVisionForms.h"

#include "JPFormBuilder.h"

#include <cstdio>

inline namespace jf {

namespace {

using Place = JPSetupProperties::Place;

std::function<JPVisionSettings&()> finder(JPConfiguration& config, const std::string& id) {
    return [&config, id]() -> JPVisionSettings& { return *config.visionSettings(id); };
}

std::string num(double v) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%g", v);
    std::string s = buf;
    if (s.find('.') == std::string::npos && s.find('e') == std::string::npos) s += ".0";
    return s;
}

// OpenPnP's enums, by their names.
void enumChoice(JPFormBuilder& add, std::function<JPVisionSettings&()> v, const char* attribute, const char* label,
                const JPFormBuilder::Strings& values) {
    const std::string def = values.front();
    add.choice(attribute, label, values, [v, attribute, def] { return v().text(attribute, def); },
               [v, attribute](const std::string& s) { v().setText(attribute, s); });
}

void general(JPFormBuilder& add, std::function<JPVisionSettings&()> v, const std::string& usedIn, bool bottom) {
    add.group("General");
    add.text("name", "Name", [v] { return v().name; }, [v](const std::string& n) { v().name = n; }, "long");
    add.text("assignedTo", "Assigned to", [usedIn] { return usedIn.empty() ? std::string("None") : usedIn; }, nullptr);
    add.row("Manage Settings");
    // On the Vision tab no part or package is in hand to specialize or generalize for.
    add.button("specialize", "Specialize", "", false);
    add.button("generalize", "Generalize", "", false);
    add.button("reset", "Reset to Default");
    add.end();
    add.flag("enabled", "Enabled?", [v] { return v().enabled; }, [v](bool on) { v().enabled = on; });
    if (!bottom) return;
    add.row("Pre-rotate");
    enumChoice(add, v, "pre-rotate-usage", "Pre-rotate", { "Default", "AlwaysOn", "AlwaysOff" });
    enumChoice(add, v, "max-rotation", "Rotation", { "Adjust", "Full" });
    add.end();
    add.row("Part size check");
    enumChoice(add, v, "check-part-size-method", "Part size check", { "Disabled", "BodySize", "PadExtents" });
    add.integer("check-size-tolerance-percent", "Size tolerance (%)",
                [v] { return v().number("check-size-tolerance-percent", 20); },
                [v](int p) { v().setText("check-size-tolerance-percent", std::to_string(p)); }, 0, 1000);
    add.end();
    add.row("Pipeline");
    add.button("editPipeline", "Edit Pipeline",
               "jplacer finds parts without a pipeline to tune: not available.", false);
    add.end();
}

void bottomForm(JPFormBuilder& add, std::function<JPVisionSettings&()> v, const std::string& usedIn) {
    add.tab("Bottom Vision Settings");
    general(add, v, usedIn, true);
    add.group("Test Alignment");
    add.row("Placement Angle");
    add.button("testAlignment", "Test Alignment", "Needs jplacer's bottom vision: not available yet.", false);
    add.end();
    add.group("Vision Offsets");
    add.flag("asymmetric", "Asymmetric?", [v] { return v().flag("asymmetric", false); },
             [v](bool on) { v().setText("asymmetric", on ? "true" : "false"); });
    add.tip("Enable if the part/package is asymmetric by design, i.e., if the contacts are offset relative to the design "
            "center of the part/package");
    add.header({ "X", "Y" });
    add.row("Vision Center Offsets");
    for (const bool x : { true, false })
        add.number(x ? "offsetX" : "offsetY", x ? "X" : "Y",
                   [v, x] {
                       const JPLocation l = v().locationOf("vision-offset").convertToUnits(JPLengthUnit::Millimeters);
                       return x ? l.x() : l.y();
                   },
                   [v, x](double d) {
                       const JPLocation l = v().locationOf("vision-offset").convertToUnits(JPLengthUnit::Millimeters);
                       v().setLocationOf("vision-offset", l.derive(x ? std::optional(d) : std::nullopt,
                                                                   x ? std::nullopt : std::optional(d), std::nullopt,
                                                                   std::nullopt));
                   });
    add.button("detectOffsets", "Detect Offsets", "Needs jplacer's bottom vision: not available yet.", false);
    add.end();
    add.tip("Offset relative to the pick location/center of the part to the center of the rectangle detected by the "
            "bottom vision");
}

void fiducialForm(JPFormBuilder& add, std::function<JPVisionSettings&()> v, const std::string& usedIn) {
    add.tab("Fiducial Vision Settings");
    general(add, v, usedIn, false);
    add.group("Fiducial Locator");
    add.integer("max-vision-passes", "Max. Vision Passes", [v] { return v().number("max-vision-passes", 3); },
                [v](int n) { v().setText("max-vision-passes", std::to_string(n)); }, 1, 100);
    add.tip("The maximum number of fiducial vision passes performed to get a good fix on the part.");
    add.number("max-linear-offset", "Max. Linear Offset", [v] { return v().lengthMm("max-linear-offset", 0.2); },
               [v](double mm) { v().setLengthMm("max-linear-offset", mm); });
    add.tip("The maximum linear fiducial offset accepted as a good fix i.e. where no additional vision pass is needed.");
    add.row("Parallax Diameter");
    add.number("parallax-diameter", "Parallax Diameter", [v] { return v().lengthMm("parallax-diameter", 0); },
               [v](double mm) { v().setLengthMm("parallax-diameter", mm); });
    add.number("parallax-angle", "Parallax Angle", [v] { return v().real("parallax-angle", 0); },
               [v](double a) { v().setText("parallax-angle", num(a)); });
    add.end();
    add.tip("When the Parallax Diameter is given, the Fiducial Locator will perform its detection from two parallax "
            "camera view-points on both sides of the expected location of the fiducial, set apart by it, and take the "
            "middle: for shiny fiducials that reflect the camera.");
    add.group("Test Fiducial Locator");
    add.row("");
    add.button("testFiducial", "Test Fiducial Locator", "Needs a fiducial part to know its size: not available here yet.",
               false);
    add.end();
}

} // namespace

JPSetupProperties::Form JPVisionForms::forSettings(JPConfiguration& config, const std::string& id, const std::string& usedIn) {
    JPSetupProperties::Form form;
    const JPVisionSettings* v = config.visionSettings(id);
    if (!v) return form;
    form.title = v->name;
    JPFormBuilder add(form);
    if (v->kind == JPVisionSettings::Kind::Bottom) bottomForm(add, finder(config, id), usedIn);
    else fiducialForm(add, finder(config, id), usedIn);
    return form;
}

bool JPVisionForms::act(JPConfiguration& config, const std::string& id, const std::string& action) {
    JPVisionSettings* v = config.visionSettings(id);
    if (!v || action != "reset") return false;
    // As the stock settings of its kind, its id and name kept.
    const JPVisionSettings* stock = config.visionSettings(v->kind == JPVisionSettings::Kind::Bottom
                                                              ? JPVisionSettings::kStockBottomId
                                                              : JPVisionSettings::kStockFiducialId);
    if (!stock || stock == v) return false;
    const std::string keepId = v->id, keepName = v->name;
    *v = *stock;
    v->id = keepId;
    v->name = keepName;
    return true;
}

} // inline namespace jf
