// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPFeeder.h"

#include "JPBlindsFeeders.h"
#include "JPFeederTape.h"

#include "JPLengthUnits.h"
#include "JPLocationXml.h"
#include "JPOpenPnpIds.h"
#include "JPXmlValues.h"

#include "openpnp/JPXmlWriter.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cmath>
#include <regex>
#include <cstdlib>

inline namespace jf {

namespace {
// The kinds that offer the feed options.
const char* const kFeedOptionsKinds[] = { "ReferenceStripFeeder", "ReferenceTrayFeeder", "ReferencePushPullFeeder",
                                          "ReferenceAutoFeeder", "ReferenceSlotAutoFeeder", "PhotonFeeder",
                                          "BambooFeederAutoVision" };
}

JPFeeder JPFeeder::create(const std::string& className, const std::string& partId) {
    JPXmlNode n("feeder");
    // A slot feeder: a "SLOT-" id and named by it, holding no part of its own (OpenPnP's).
    const std::string simple = simpleName(className);
    const bool slot = simple == "ReferenceSlotAutoFeeder" || simple == "SlotSchultzFeeder";
    const std::string id = JPOpenPnpIds::create(slot ? "SLOT-" : "FDR");
    n.attr("class", className).attr("version", "1.1").attr("id", id)
        .attr("name", slot ? id : simple).attr("enabled", "false").attr("part-id", slot ? std::string() : partId)
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

bool JPFeeder::isSlot() const {
    const std::string t = typeName();
    return t == "ReferenceSlotAutoFeeder" || t == "SlotSchultzFeeder";
}

std::string JPFeeder::feedsAs() const {
    const std::string t = typeName();
    if (t == "ReferenceSlotAutoFeeder") return "ReferenceAutoFeeder";
    if (t == "SlotSchultzFeeder") return "SchultzFeeder";
    return t;
}

std::string JPFeeder::name() const {
    if (isPhoton()) {
        // As OpenPnP: "Unconfigured PhotonFeeder" until it has a hardware id; then its slot.
        if (text("hardware-id").empty()) return "Unconfigured " + typeName();
        return text("name") + " (Slot: " + (photonSlot ? std::to_string(*photonSlot) : std::string("None")) + ")";
    }
    if (!isSlot()) return text("name");
    return text("name") + " (" + (slotLoad ? slotLoad->feederName : std::string("None")) + ")";
}

void JPFeeder::setName(const std::string& n) {
    std::string plain = n;
    if (isPhoton()) {
        // OpenPnP's: every "(Slot: …)" taken off.
        plain = std::regex_replace(plain, std::regex(R"(\(Slot: [\w+]+\))"), "");
        while (!plain.empty() && plain.back() == ' ') plain.pop_back();
        while (!plain.empty() && plain.front() == ' ') plain.erase(0, 1);
    }
    if (isSlot()) {
        // OpenPnP's: what it shows of the loaded feeder taken off.
        const std::string shown = "(" + (slotLoad ? slotLoad->feederName : std::string("None")) + ")";
        if (const size_t at = plain.find(shown); at != std::string::npos) plain.erase(at, shown.size());
        while (!plain.empty() && plain.back() == ' ') plain.pop_back();
        while (!plain.empty() && plain.front() == ' ') plain.erase(0, 1);
    }
    setText("name", plain);
}

bool JPFeeder::enabled() const {
    const bool on = flag("enabled", false);
    if (isPhoton()) return on && !text("hardware-id").empty() && !text("part-id").empty() && photonUnconfigured().empty();
    return isSlot() ? on && slotLoad && !slotLoad->partId.empty() : on;
}

std::string JPFeeder::photonUnconfigured() const {
    const std::string id = text("hardware-id");
    if (!photonSlot) return "Photon Feeder with address " + id + " has no address. Is it inserted?";
    if (!photonSlotLocation) return "The slot at address " + std::to_string(*photonSlot) + " has no location configured.";
    if (!m_node.child("offset")) return "Photon Feeder with address " + id + " has no location offset.";
    return {};
}

std::string JPFeeder::partId() const {
    if (!isSlot()) return text("part-id");
    return slotLoad ? slotLoad->partId : std::string();
}

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

bool JPFeeder::canTakeBackPart() const {
    const std::string t = feedsAs();
    if (t == "ReferenceStripFeeder" || t == "ReferenceTrayFeeder" || t == "ReferenceRotatedTrayFeeder" || t == "BlindsFeeder")
        return number("feed-count") > 0;
    if (t == "ReferenceAutoFeeder") return flag("recycle-support", false) && feedOptions() != FeedOptions::SkipNext;
    if (t == "ReferencePushPullFeeder" || t == "BambooFeederAutoVision") return feedOptions() != FeedOptions::SkipNext;
    if (t == "PhotonFeeder") return feedOptions() == FeedOptions::Normal;
    if (t == "ReferenceLoosePartFeeder" || t == "AdvancedLoosePartFeeder") return foundPick.has_value();
    return t == "ReferenceHeapFeeder";
}

void JPFeeder::partTakenBack() {
    const std::string t = feedsAs();
    if (t == "ReferenceStripFeeder" || t == "ReferenceTrayFeeder") {
        if (feedOptions() == FeedOptions::Normal) setNumber("feed-count", number("feed-count") - 1);
    } else if (t == "ReferenceRotatedTrayFeeder" || t == "BlindsFeeder") {
        setNumber("feed-count", number("feed-count") - 1);
    } else if (t == "ReferenceAutoFeeder" || t == "ReferencePushPullFeeder" || t == "BambooFeederAutoVision" || t == "PhotonFeeder") {
        setFeedOptions(FeedOptions::SkipNext);
    } else if (t == "ReferenceLoosePartFeeder" || t == "AdvancedLoosePartFeeder") {
        foundPick.reset();   // not a second part on the same place
    }
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

double mm(const JPLength& l) { return l.convertToUnits(JPLengthUnit::Millimeters).value(); }

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
    if (kind == "ReferenceRotatedTrayFeeder") {
        const int cols = std::max(number("tray-count-cols", 1), 1), rows = std::max(number("tray-count-rows", 1), 1);
        const int count = number("feed-count");
        int base0 = count - 1;
        if (count == 0) base0 = 0;
        else if (count > cols * rows) base0 = cols * rows - 1;   // past the end: the last
        // Along a row, then the next (an older file part way through: along a column).
        int col, row;
        if (flag("legacy-picking-in-progress", false) && cols >= rows) {
            row = base0 % rows;
            col = base0 / rows;
        } else {
            row = base0 / cols;
            col = base0 % cols;
        }
        // Rows go the tray's negative Y; the steps turned with the tray.
        const JPLocation l = location();
        const JPLocation step = locationOf("offsets").convertToUnits(l.units());
        const double a = l.rotation() * M_PI / 180;
        const double dx = step.x() * col, dy = -step.y() * row;
        return JPLocation(l.units(), l.x() + dx * std::cos(a) - dy * std::sin(a), l.y() + dx * std::sin(a) + dy * std::cos(a),
                          l.z(), l.rotation() + real("component-rotation-in-tray", 0));
    }
    // Picked where they are set.
    if (isSlot()) return slotLoad ? slotLoad->offsets.offsetWithRotationFrom(location()) : location();
    if (isPhoton()) {
        if (!photonUnconfigured().empty()) return std::nullopt;
        return locationOf("offset").offsetWithRotationFrom(*photonSlotLocation);
    }
    // The pocket fed, in its holder's frame.
    if (kind == "BlindsFeeder") return JPBlindsFeeders::pickLocation(*this, JPBlindsFeeders::fedPocket(*this));
    // Where its tape's frame puts the part in the feed cycle (OpenPnP's 1-based count: at 0, the last).
    if (isVisionTape()) {
        const JPFeederTape::Params tape = JPFeederTape::of(*this);
        const long cycle = JPFeederTape::partsPerFeedCycle(tape);
        const long partInCycle = ((long(number("feed-count")) + cycle - 1) % cycle) + 1;
        return JPFeederTape::partLocation(partInCycle, visionOffset, tape, real("rotation-in-feeder", 0));
    }
    // Where its pipeline found the part, else where it is.
    if (kind == "ReferenceLoosePartFeeder" || kind == "AdvancedLoosePartFeeder" || kind == "ReferenceHeapFeeder")
        return foundPick ? *foundPick : location();
    if (kind == "ReferenceTubeFeeder" || kind == "ReferenceAutoFeeder" || kind == "RapidFeeder" || kind == "SchultzFeeder")
        return location();
    if (kind == "Neoden4Feeder") {
        // Turned by its rotation in the tape; moved by where its vision last found the template.
        JPLocation at = location();
        at = at.derive(std::nullopt, std::nullopt, std::nullopt,
                       at.rotation() + std::atoi(childText("part-rotation-in-tape", "0").c_str()));
        if (attributeAt("vision", "enabled") == "true" && templateOffset) at = at.subtract(*templateOffset);
        return at;
    }
    if (kind == "ReferenceLeverFeeder") {
        // As OpenPnP's: the second part's step only with vision.
        JPLocation at = location();
        const double pitch = lengthOf("part-pitch", JPLength(4, JPLengthUnit::Millimeters)).convertToUnits(JPLengthUnit::Millimeters).value();
        if (attributeAt("vision", "enabled") == "true" && templateOffset) {
            at = at.subtract(*templateOffset);
            if (pitch == 2 && nextPartPick) at = at.add(*nextPartPick);
        }
        return at;
    }
    if (kind == "ReferenceDragFeeder") {
        JPLocation at = location();
        const double pitch = lengthOf("part-pitch", JPLength(4, JPLengthUnit::Millimeters)).convertToUnits(JPLengthUnit::Millimeters).value();
        if (pitch == 2 && nextPartPick) at = at.add(*nextPartPick);
        if (attributeAt("vision", "enabled") == "true" && templateOffset) at = at.subtract(*templateOffset);
        return at;
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
        double angle = b.angleTo(a) - 90;
        const double r = angle * M_PI / 180;
        l = l.add(JPLocation(l.units(), x * std::cos(r) - y * std::sin(r), x * std::sin(r) + y * std::cos(r), 0, 0));
        if (flag("standard-eia-481", true)) angle += 90;
        return l.derive(std::nullopt, std::nullopt, std::nullopt, angle + location().rotation());
    }
    return std::nullopt;
}

JPFeeder JPFeeder::fromXml(const JPXmlElement& e) {
    JPFeeder f(JPXmlNode::from(e));
    if (f.typeName() == "ReferenceStripFeeder" && f.text("standard-eia-481").empty()) {
        // OpenPnP's angleNorm(rotation - 90, 180): within -180 (exclusive) .. 180.
        double r = f.location().rotation() - 90;
        while (r > 180) r -= 360;
        while (r <= -180) r += 360;
        const JPLocation l = f.location();
        f.setLocation(l.derive(std::nullopt, std::nullopt, std::nullopt, r));
        f.setFlag("standard-eia-481", true);
    }
    return f;
}

bool JPFeeder::partHeightAbovePickLocation() const {
    const std::string kind = typeName();
    return kind == "ReferenceLoosePartFeeder" || kind == "AdvancedLoosePartFeeder";
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
    if (kind == "ReferenceRotatedTrayFeeder") {
        const int cols = std::max(number("tray-count-cols", 1), 1), rows = std::max(number("tray-count-rows", 1), 1);
        if (number("feed-count") >= cols * rows) {
            why = name() + " (" + partId() + ") is empty.";
            if (empty) *empty = true;
            return false;
        }
        setNumber("feed-count", number("feed-count") + 1);
        return true;
    }
    if (isSlot() && !slotLoad) {
        why = "No feeder loaded in slot.";
        return false;
    }
    if (kind == "SlotSchultzFeeder") return true;
    if (kind == "ReferenceSlotAutoFeeder") {
        m_actuate = feedOptions() == FeedOptions::Normal;
        if (feedOptions() == FeedOptions::SkipNext) setFeedOptions(FeedOptions::Normal);
        return true;
    }
    // A tube: nothing to do; a drag, lever, Rapid, Schultz, Neoden 4, Photon, loose part, heap, blinds or Bamboo feeder's feed is the machine's (JPFeederFeed). An auto feeder: its actuator, on a normal feed (JPFeederFeed).
    if (kind == "ReferenceTubeFeeder" || kind == "ReferenceDragFeeder" || kind == "ReferenceLeverFeeder" || kind == "RapidFeeder"
        || kind == "SchultzFeeder" || kind == "Neoden4Feeder" || kind == "PhotonFeeder" || kind == "ReferenceLoosePartFeeder"
        || kind == "AdvancedLoosePartFeeder" || kind == "ReferenceHeapFeeder" || kind == "BlindsFeeder" || isVisionTape())
        return true;
    if (kind == "ReferenceAutoFeeder") {
        m_actuate = feedOptions() == FeedOptions::Normal;
        if (feedOptions() == FeedOptions::SkipNext) setFeedOptions(FeedOptions::Normal);
        return true;
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
    // A strip's vision, as OpenPnP's: the first hole when the first pick is
    // mid-strip, then the hole fed (not when its pick is repeated).
    if (kind == "ReferenceStripFeeder" && flag("vision-enabled", false)) {
        const int count = number("feed-count");
        if (count != 1 && !visionLocationReference) m_visionChecks.push_back(1);
        if (feedOptions() == FeedOptions::Normal) m_visionChecks.push_back(count);
    }
    if (feedOptions() == FeedOptions::SkipNext) setFeedOptions(FeedOptions::Normal);
    return true;
}

bool JPFeeder::takeFeedActuation() {
    const bool a = m_actuate;
    m_actuate = false;
    return a;
}

std::vector<int> JPFeeder::takeVisionChecks() {
    std::vector<int> out;
    out.swap(m_visionChecks);
    return out;
}

std::optional<JPLocation> JPFeeder::visionExpected(int n) const {
    if (!flag("vision-enabled", false)) return std::nullopt;
    const auto [a, b] = idealLineLocations();
    const double pitch = mm(lengthOf("part-pitch", JPLength(4, JPLengthUnit::Millimeters)));
    const double holePitchMm = mm(holePitch());
    // Under 4 mm a hole serves two parts: each looked at twice.
    const double along = pitch < 4 ? holePitchMm * double((n - 1) / 2) : pitch * double(n - 1);
    const JPLocation expected = pointAlongLine(a, b, JPLength(along, JPLengthUnit::Millimeters).convertToUnits(a.units()).value());
    if (!visionLocation) return expected;
    const JPLocation e = expected.convertToUnits(JPLengthUnit::Millimeters), v = visionLocation->convertToUnits(JPLengthUnit::Millimeters);
    const double toVision = std::hypot(e.x() - v.x(), e.y() - v.y());
    // The same hole (within half a pitch): not again.
    if (toVision < holePitchMm * 0.5) return std::nullopt;
    // Far enough from the last found: looked at. Near the strip's start the
    // distance grows with the span measured (OpenPnP's geometric progression).
    const JPLocation aa = a.convertToUnits(JPLengthUnit::Millimeters), bb = b.convertToUnits(JPLengthUnit::Millimeters);
    const double span = std::hypot(bb.x() - aa.x(), bb.y() - aa.y());
    const double extrapolation = std::min(mm(lengthOf("extrapolation-distance", JPLength(0, JPLengthUnit::Millimeters))) * 1.01,
                                          span * 0.7);
    if (toVision >= extrapolation) return expected;
    return std::nullopt;
}

void JPFeeder::setVisionFound(int n, const JPLocation& found) {
    if (n == 1) visionLocationReference = found;
    visionLocation = found;
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

void JPFeeder::removeAttribute(const std::string& attribute) {
    std::erase_if(m_node.attributes, [&attribute](const auto& a) { return a.first == attribute; });
}

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

void JPFeeder::setChild(JPXmlNode node) {
    if (JPXmlNode* c = m_node.child(node.name)) *c = std::move(node);
    else m_node.add(std::move(node));
}

void JPFeeder::removeChild(const std::string& element) {
    std::erase_if(m_node.children, [&element](const JPXmlNode& c) { return c.name == element; });
}

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
    // As OpenPnP's setLocation, setHole1Location and setHole2Location: the calibration reset.
    if (isVisionTape() && (element == "location" || element == "hole-1-location" || element == "hole-2-location"))
        visionOffset.reset();
}

void JPFeeder::resetVisionOffsets() {
    templateOffset.reset();
    nextPartPick.reset();
}

std::string JPFeeder::templateDirectory(const std::string& configDirectory, const std::string& className) {
    return configDirectory + "/" + className + ".Vision";
}

std::string JPFeeder::templatePath(const std::string& configDirectory) const {
    const std::string name = attributeAt("vision", "template-image-name");
    return name.empty() ? std::string() : templateDirectory(configDirectory, className()) + "/" + name;
}

std::string JPFeeder::attributeAt(const std::string& path, const std::string& attribute, const std::string& def) const {
    const JPXmlNode* n = &m_node;
    size_t from = 0;
    while (n && from <= path.size()) {
        const size_t slash = path.find('/', from);
        n = n->child(path.substr(from, slash == std::string::npos ? std::string::npos : slash - from));
        if (slash == std::string::npos) break;
        from = slash + 1;
    }
    const std::string* v = n ? n->get(attribute) : nullptr;
    return v ? *v : def;
}

void JPFeeder::setAttributeAt(const std::string& path, const std::string& attribute, const std::string& value) {
    JPXmlNode* n = &m_node;
    size_t from = 0;
    for (;;) {
        const size_t slash = path.find('/', from);
        const std::string name = path.substr(from, slash == std::string::npos ? std::string::npos : slash - from);
        JPXmlNode* c = n->child(name);
        n = c ? c : &n->add(JPXmlNode(name));
        if (slash == std::string::npos) break;
        from = slash + 1;
    }
    n->set(attribute, value);
}

void JPFeeder::setPipeline(JPXmlNode pipeline, const std::string& element) {
    pipeline.name = element;
    if (JPXmlNode* c = m_node.child(element)) {
        *c = std::move(pipeline);
        return;
    }
    m_node.add(std::move(pipeline));
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
