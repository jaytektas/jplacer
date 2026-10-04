// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPVisionForms.h"

#include "JPFormBuilder.h"

#include "model/JPOpenPnpIds.h"

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

void general(JPFormBuilder& add, std::function<JPVisionSettings&()> v, const std::string& usedIn, bool bottom,
             const JPVisionForms::Holder& holder) {
    using Kind = JPVisionForms::Holder::Kind;
    const std::string prefix = bottom ? "bottom:" : "fiducial:";
    const std::string what = bottom ? "Bottom Vision Settings" : "Fiducial Vision Settings";
    add.group("General");
    add.text(prefix + "name", "Name", [v] { return v().name; }, [v](const std::string& n) { v().name = n; }, "long");
    add.text(prefix + "assignedTo", "Assigned to", [usedIn] { return usedIn.empty() ? std::string("None") : usedIn; }, nullptr);
    add.row("Manage Settings");
    // Shown from a part or a package: for it; on the Vision tab nothing is in hand.
    const std::string kindName = holder.kind == Kind::Part ? "Part" : "Package";
    if (holder.kind == Kind::None)
        add.button(prefix + "specialize", "Specialize", "", false);
    else
        add.button(prefix + "specialize", "Specialize for " + holder.id,
                   "Create a copy of these " + what + " and assign to " + kindName + " " + holder.id);
    if (holder.kind == Kind::Package)
        add.button(prefix + "generalize", "Generalize for " + holder.id,
                   "Generalize these " + what + " for all the Parts with the Package " + holder.id +
                       ". This will unassign any special " + what + " on Parts.");
    else
        add.button(prefix + "generalize", "Generalize", "", false);
    add.button(prefix + "reset", "Reset to Default");
    add.end();
    add.flag(prefix + "enabled", "Enabled?", [v] { return v().enabled; }, [v](bool on) { v().enabled = on; });
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
    add.button(prefix + "editPipeline", "Edit Pipeline", "jplacer finds parts without a pipeline to tune: not available.",
               false);
    add.end();
}

void bottomForm(JPFormBuilder& add, std::function<JPVisionSettings&()> v, const std::string& usedIn,
                const JPVisionForms::Holder& holder) {
    add.tab("Bottom Vision Settings");
    general(add, v, usedIn, true, holder);
    add.group("Test Alignment");
    add.row("Placement Angle");
    add.button("bottom:testAlignment", "Test Alignment", "Needs jplacer's bottom vision: not available yet.", false);
    add.end();
    add.group("Vision Offsets");
    add.flag("bottom:asymmetric", "Asymmetric?", [v] { return v().flag("asymmetric", false); },
             [v](bool on) { v().setText("asymmetric", on ? "true" : "false"); });
    add.tip("Enable if the part/package is asymmetric by design, i.e., if the contacts are offset relative to the design "
            "center of the part/package");
    add.header({ "X", "Y" });
    add.row("Vision Center Offsets");
    for (const bool x : { true, false })
        add.number(x ? "bottom:offsetX" : "bottom:offsetY", x ? "X" : "Y",
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
    add.button("bottom:detectOffsets", "Detect Offsets", "Needs jplacer's bottom vision: not available yet.", false);
    add.end();
    add.tip("Offset relative to the pick location/center of the part to the center of the rectangle detected by the "
            "bottom vision");
}

void fiducialForm(JPFormBuilder& add, std::function<JPVisionSettings&()> v, const std::string& usedIn,
                  const JPVisionForms::Holder& holder) {
    add.tab("Fiducial Vision Settings");
    general(add, v, usedIn, false, holder);
    add.group("Fiducial Locator");
    add.integer("fiducial:max-vision-passes", "Max. Vision Passes", [v] { return v().number("max-vision-passes", 3); },
                [v](int n) { v().setText("max-vision-passes", std::to_string(n)); }, 1, 100);
    add.tip("The maximum number of fiducial vision passes performed to get a good fix on the part.");
    add.number("fiducial:max-linear-offset", "Max. Linear Offset", [v] { return v().lengthMm("max-linear-offset", 0.2); },
               [v](double mm) { v().setLengthMm("max-linear-offset", mm); });
    add.tip("The maximum linear fiducial offset accepted as a good fix i.e. where no additional vision pass is needed.");
    add.row("Parallax Diameter");
    add.number("fiducial:parallax-diameter", "Parallax Diameter", [v] { return v().lengthMm("parallax-diameter", 0); },
               [v](double mm) { v().setLengthMm("parallax-diameter", mm); });
    add.number("fiducial:parallax-angle", "Parallax Angle", [v] { return v().real("parallax-angle", 0); },
               [v](double a) { v().setText("parallax-angle", num(a)); });
    add.end();
    add.tip("When the Parallax Diameter is given, the Fiducial Locator will perform its detection from two parallax "
            "camera view-points on both sides of the expected location of the fiducial, set apart by it, and take the "
            "middle: for shiny fiducials that reflect the camera.");
    add.group("Test Fiducial Locator");
    add.row("");
    add.button("fiducial:testFiducial", "Test Fiducial Locator", "Needs a fiducial part to know its size: not available here yet.",
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
    addPage(add, config, id, usedIn, Holder {});
    return form;
}

void JPVisionForms::addPage(JPFormBuilder& add, JPConfiguration& config, const std::string& id, const std::string& usedIn,
                            const Holder& holder) {
    const JPVisionSettings* v = config.visionSettings(id);
    if (!v) return;
    if (v->kind == JPVisionSettings::Kind::Bottom) bottomForm(add, finder(config, id), usedIn, holder);
    else fiducialForm(add, finder(config, id), usedIn, holder);
}

std::vector<std::string> JPVisionForms::specializedIn(const JPConfiguration& config, const Holder& holder,
                                                      JPVisionSettings::Kind kind) {
    std::vector<std::string> out;
    if (holder.kind != Holder::Kind::Package) return out;
    for (const auto& p : config.parts())
        if (p->packageId == holder.id
            && !(kind == JPVisionSettings::Kind::Bottom ? p->bottomVisionId : p->fiducialVisionId).empty())
            out.push_back(p->id);
    return out;
}

bool JPVisionForms::act(JPConfiguration& config, const std::string& id, const std::string& action, const Holder& holder,
                        std::string& why) {
    why.clear();
    JPVisionSettings* v = config.visionSettings(id);
    if (!v) return false;
    const bool bottom = v->kind == JPVisionSettings::Kind::Bottom;
    if (action == "reset") {
        // As the stock settings of its kind, its id and name kept.
        const JPVisionSettings* stock =
            config.visionSettings(bottom ? JPVisionSettings::kStockBottomId : JPVisionSettings::kStockFiducialId);
        if (!stock || stock == v) return false;
        const std::string keepId = v->id, keepName = v->name;
        *v = *stock;
        v->id = keepId;
        v->name = keepName;
        return true;
    }
    if (action == "specialize" && holder.kind != Holder::Kind::None) {
        // Already its own: nothing to copy.
        const std::vector<std::string> used = config.visionUsedIn(*v, "", "");
        if (used.size() == 1 && used.front() == holder.id) {
            why = "Vision Settings already specialized for " + holder.id + ".";
            return false;
        }
        JPVisionSettings copy = *v;
        copy.id = JPOpenPnpIds::create(bottom ? "BVS" : "FVS");
        copy.name = holder.id;
        const std::string newId = copy.id;
        config.addVisionSettings(copy);
        if (holder.kind == Holder::Kind::Part) {
            if (JPPart* p = config.part(holder.id)) (bottom ? p->bottomVisionId : p->fiducialVisionId) = newId;
        } else if (JPPackage* p = config.package(holder.id)) {
            (bottom ? p->bottomVisionId : p->fiducialVisionId) = newId;
        }
        return true;
    }
    if (action == "generalize" && holder.kind == Holder::Kind::Package) {
        for (const std::string& partId : specializedIn(config, holder, v->kind))
            if (JPPart* p = config.part(partId)) (bottom ? p->bottomVisionId : p->fiducialVisionId).clear();
        return true;
    }
    return false;
}

} // inline namespace jf
