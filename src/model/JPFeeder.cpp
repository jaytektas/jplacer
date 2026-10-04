// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPFeeder.h"

#include "JPLengthUnits.h"
#include "JPLocationXml.h"
#include "JPXmlValues.h"

#include "openpnp/JPXmlWriter.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cmath>
#include <cstdlib>

inline namespace jf {

namespace {
// The kinds that offer the feed options.
const char* const kFeedOptionsKinds[] = { "ReferenceStripFeeder", "ReferenceTrayFeeder", "ReferencePushPullFeeder",
                                          "ReferenceAutoFeeder", "PhotonFeeder", "BambooFeederAutoVision" };
}

JPFeeder JPFeeder::create(const std::string& className, const std::string& partId) {
    const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch());
    static long long last = 0;   // as OpenPnP's NanosecondTime: never the same twice
    long long now = ns.count();
    if (now <= last) now = last + 1;
    last = now;
    char hex[32];
    std::snprintf(hex, sizeof hex, "%llx", now);
    JPXmlNode n("feeder");
    n.attr("class", className).attr("version", "1.1").attr("id", std::string("FDR") + hex)
        .attr("name", simpleName(className)).attr("enabled", "false").attr("part-id", partId)
        .attr("feed-retry-count", "3").attr("pick-retry-count", "3").attr("priority", "Normal");
    n.add(JPLocationXml::to("location", JPLocation(JPLengthUnit::Millimeters)));
    return JPFeeder(std::move(n));
}

const std::vector<std::string>& JPFeeder::classNames() {
    static const std::vector<std::string> names {
        "org.openpnp.machine.reference.feeder.ReferenceStripFeeder",
        "org.openpnp.machine.reference.feeder.ReferenceTrayFeeder",
        "org.openpnp.machine.reference.feeder.ReferenceRotatedTrayFeeder",
        "org.openpnp.machine.reference.feeder.ReferenceDragFeeder",
        "org.openpnp.machine.reference.feeder.ReferenceLeverFeeder",
        "org.openpnp.machine.reference.feeder.ReferencePushPullFeeder",
        "org.openpnp.machine.reference.feeder.ReferenceTubeFeeder",
        "org.openpnp.machine.reference.feeder.ReferenceAutoFeeder",
        "org.openpnp.machine.reference.feeder.ReferenceSlotAutoFeeder",
        "org.openpnp.machine.reference.feeder.ReferenceLoosePartFeeder",
        "org.openpnp.machine.reference.feeder.AdvancedLoosePartFeeder",
        "org.openpnp.machine.reference.feeder.ReferenceHeapFeeder",
        "org.openpnp.machine.reference.feeder.BlindsFeeder",
        "org.openpnp.machine.reference.feeder.SchultzFeeder",
        "org.openpnp.machine.reference.feeder.SlotSchultzFeeder",
        "org.openpnp.machine.rapidplacer.RapidFeeder",
        "org.openpnp.machine.neoden4.Neoden4Feeder",
        "org.openpnp.machine.photon.PhotonFeeder",
        "org.openpnp.machine.pandaplacer.BambooFeederAutoVision",
    };
    return names;
}

std::string JPFeeder::simpleName(const std::string& className) {
    const size_t dot = className.rfind('.');
    return dot == std::string::npos ? className : className.substr(dot + 1);
}

std::string JPFeeder::className() const { return text("class"); }

std::string JPFeeder::typeName() const { return simpleName(className()); }

void JPFeeder::setEnabled(bool on) {
    setFlag("enabled", on);
    if (on) m_jobFaults.clear();   // as OpenPnP: turned on, its faults forgotten
}

JPFeeder::Priority JPFeeder::priority() const {
    const std::string p = text("priority", "Normal");
    return p == "High" ? Priority::High : p == "Low" ? Priority::Low : Priority::Normal;
}

void JPFeeder::setPriority(Priority p) { setText("priority", priorityName(p)); }

bool JPFeeder::supportsFeedOptions() const {
    const std::string t = typeName();
    return std::any_of(std::begin(kFeedOptionsKinds), std::end(kFeedOptionsKinds), [&t](const char* k) { return t == k; });
}

JPFeeder::FeedOptions JPFeeder::feedOptions() const {
    const std::string o = text("feed-options", "Normal");
    return o == "SkipNext" ? FeedOptions::SkipNext : o == "Disable" ? FeedOptions::Disable : FeedOptions::Normal;
}

void JPFeeder::setFeedOptions(FeedOptions o) {
    setText("feed-options", o == FeedOptions::SkipNext ? "SkipNext" : o == FeedOptions::Disable ? "Disable" : "Normal");
}

const char* JPFeeder::priorityName(Priority p) {
    return p == Priority::High ? "High" : p == Priority::Low ? "Low" : "Normal";
}

const char* JPFeeder::feedOptionsName(FeedOptions o) {
    return o == FeedOptions::SkipNext ? "Skip next feed" : o == FeedOptions::Disable ? "Disable feed" : "Normal feed";
}

namespace {

JPLocation pointAlongLine(const JPLocation& a, const JPLocation& bIn, double distance) {
    const JPLocation b = bIn.convertToUnits(a.units());
    const double dx = b.x() - a.x(), dy = b.y() - a.y(), l = std::hypot(dx, dy);
    if (l == 0) return a;
    return a.add(JPLocation(a.units(), dx / l * distance, dy / l * distance, 0, 0));
}

double angleFromPoint(const JPLocation& first, const JPLocation& secondIn) {
    const JPLocation second = secondIn.convertToUnits(first.units());
    return std::atan2(second.y() - first.y(), second.x() - first.x()) * 180 / M_PI;
}

} // namespace

std::pair<JPLocation, JPLocation> JPFeeder::idealLineLocations() const {
    const JPLocation ref = locationOf("reference-hole-location"), last = locationOf("last-hole-location");
    if (!visionLocation) return { ref, last };
    JPLocation r = ref, l = last;
    if (visionLocationReference) {
        r = *visionLocationReference;
        l = last.add(*visionLocationReference).subtract(ref);
    }
    auto mm = [](const JPLocation& a, const JPLocation& b) {
        const JPLocation x = a.convertToUnits(JPLengthUnit::Millimeters), y = b.convertToUnits(JPLengthUnit::Millimeters);
        return std::hypot(x.x() - y.x(), x.y() - y.y());
    };
    // Towards the vision location when it is about as far as the last hole.
    if (mm(r, *visionLocation) > mm(r, l) * 0.9) return { r, *visionLocation };
    return { r, l };
}

std::optional<JPLocation> JPFeeder::pickLocation() const {
    const std::string kind = typeName();
    if (kind == "ReferenceTrayFeeder") {
        const int cx = std::max(number("tray-count-x", 1), 1), cy = std::max(number("tray-count-y", 1), 1);
        const int count = number("feed-count");
        int base0 = count - 1;
        if (count == 0) base0 = 0;
        else if (count > cx * cy) base0 = cx * cy - 1;   // past the end: the last
        int px, py;
        if (cx >= cy) {
            px = base0 / cy;
            py = base0 % cy;
        } else {
            px = base0 % cx;
            py = base0 / cx;
        }
        return location().add(locationOf("offsets").multiply(px, py, 0.0, 0.0));
    }
    if (kind == "ReferenceStripFeeder") {
        // Before a feed, the first part (not off the strip's end).
        const int count = std::max(1, number("feed-count"));
        const auto [a, b] = idealLineLocations();
        const JPLength partPitch = lengthOf("part-pitch", JPLength(4, JPLengthUnit::Millimeters));
        const JPLength tapeWidth = lengthOf("tape-width", JPLength(8, JPLengthUnit::Millimeters));
        const double holePitch = 4;   // EIA-481's, in mm
        const JPLocation bb = b.convertToUnits(a.units());
        double pitch = std::hypot(bb.x() - a.x(), bb.y() - a.y());
        const double holes = std::round(pitch / holePitch);
        pitch = holes > 0 ? partPitch.value() / holePitch * pitch / holes : holePitch;
        JPLocation l = pointAlongLine(a, b, JPLength((count - 1) * pitch, partPitch.units()).convertToUnits(a.units()).value());
        // From the hole to the part: across the tape, and 2 mm along it.
        const double x = JPLength(tapeWidth.convertToUnits(JPLengthUnit::Millimeters).value() / 2 - 0.5, JPLengthUnit::Millimeters)
                             .convertToUnits(l.units()).value();
        const double y = JPLength(2, JPLengthUnit::Millimeters).convertToUnits(l.units()).value();
        double angle = angleFromPoint(b, a) - 90;
        const double r = angle * M_PI / 180;
        l = l.add(JPLocation(l.units(), x * std::cos(r) - y * std::sin(r), x * std::sin(r) + y * std::cos(r), 0, 0));
        if (flag("standard-eia-481", true)) angle += 90;
        return l.derive(std::nullopt, std::nullopt, std::nullopt, angle + location().rotation());
    }
    return std::nullopt;
}

bool JPFeeder::feed(std::string& why, bool* empty) {
    if (empty) *empty = false;
    const std::string kind = typeName();
    if (kind == "ReferenceTrayFeeder") {
        const int cx = std::max(number("tray-count-x", 1), 1), cy = std::max(number("tray-count-y", 1), 1);
        if (number("feed-count") >= cx * cy) {
            why = "Feeder: " + name() + " (" + partId() + ") - tray empty.";
            if (empty) *empty = true;
            return false;
        }
    }
    if (kind != "ReferenceTrayFeeder" && kind != "ReferenceStripFeeder") {
        why = "Feeding a " + kind + " is not available yet.";
        return false;
    }
    if (feedOptions() == FeedOptions::Normal || number("feed-count") == 0) setNumber("feed-count", number("feed-count") + 1);
    if (kind == "ReferenceStripFeeder") {
        const int most = number("max-feed-count");
        if (most > 0 && number("feed-count") > most) {
            why = "Tried to feed part: " + partId() + "  Feeder " + name() + " empty.";
            if (empty) *empty = true;
            return false;
        }
    }
    if (feedOptions() == FeedOptions::SkipNext) setFeedOptions(FeedOptions::Normal);
    return true;
}

std::string JPFeeder::summariseJobFaults() const {
    std::string r;
    bool any = false;
    for (bool f : m_jobFaults) {
        any = any || f;
        r += f ? 'X' : '-';
    }
    return any ? r : std::string();
}

void JPFeeder::addJobFault(bool fault, int windowSize) {
    m_jobFaults.push_front(fault);
    while (int(m_jobFaults.size()) > std::max(1, windowSize)) m_jobFaults.pop_back();
}

void JPFeeder::recordJobSuccess(int windowSize) { addJobFault(false, windowSize); }

void JPFeeder::recordJobFault(int faultLimit, int windowSize) {
    addJobFault(true, windowSize);
    const long faults = std::count(m_jobFaults.begin(), m_jobFaults.end(), true);
    // As OpenPnP: too many faults in the window turns it off.
    if (enabled() && faultLimit > 0 && faults >= faultLimit) setFlag("enabled", false);
}

std::string JPFeeder::text(const std::string& attribute, const std::string& def) const {
    const std::string* v = m_node.get(attribute);
    return v ? *v : def;
}

void JPFeeder::setText(const std::string& attribute, const std::string& value) { m_node.set(attribute, value); }

int JPFeeder::number(const std::string& attribute, int def) const {
    const std::string* v = m_node.get(attribute);
    return v && !v->empty() ? std::atoi(v->c_str()) : def;
}

void JPFeeder::setNumber(const std::string& attribute, int value) { m_node.set(attribute, std::to_string(value)); }

double JPFeeder::real(const std::string& attribute, double def) const {
    const std::string* v = m_node.get(attribute);
    return v && !v->empty() ? std::strtod(v->c_str(), nullptr) : def;
}

void JPFeeder::setReal(const std::string& attribute, double value) { m_node.set(attribute, JPXmlWriter::number(value)); }

bool JPFeeder::flag(const std::string& attribute, bool def) const {
    const std::string* v = m_node.get(attribute);
    return v ? *v == "true" : def;
}

void JPFeeder::setFlag(const std::string& attribute, bool on) { m_node.set(attribute, on ? "true" : "false"); }

JPLocation JPFeeder::locationOf(const std::string& element) const {
    const JPXmlNode* c = m_node.child(element);
    if (!c) return JPLocation(JPLengthUnit::Millimeters);
    JPXmlElement e;
    e.name = c->name;
    for (const auto& [k, v] : c->attributes) e.attributes[k] = v;
    return JPLocationXml::from(e);
}

void JPFeeder::setLocationOf(const std::string& element, const JPLocation& l) {
    JPXmlNode fresh = JPLocationXml::to(element, l);
    if (JPXmlNode* c = m_node.child(element)) *c = fresh;
    else m_node.add(fresh);
}

std::string JPFeeder::childText(const std::string& element, const std::string& def) const {
    const JPXmlNode* c = m_node.child(element);
    return c ? c->text : def;
}

void JPFeeder::setChildText(const std::string& element, const std::string& value) {
    if (JPXmlNode* c = m_node.child(element)) {
        c->text = value;
        return;
    }
    JPXmlNode fresh(element);
    fresh.text = value;
    m_node.add(std::move(fresh));
}

JPLength JPFeeder::lengthOf(const std::string& element, const JPLength& def) const {
    const JPXmlNode* c = m_node.child(element);
    if (!c || !c->get("value")) return def;
    JPLengthUnit u = JPLengthUnit::Millimeters;
    if (const std::string* units = c->get("units")) JPLengthUnits::fromName(*units, u);
    return JPLength(std::strtod(c->get("value")->c_str(), nullptr), u);
}

void JPFeeder::setLengthOf(const std::string& element, const JPLength& l) {
    JPXmlNode fresh(element);
    fresh.attr("value", JPXmlWriter::number(l.value())).attr("units", JPLengthUnits::name(l.units()));
    if (JPXmlNode* c = m_node.child(element)) *c = fresh;
    else m_node.add(fresh);
}

} // inline namespace jf
