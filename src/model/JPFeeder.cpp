// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPFeeder.h"

#include "JPBlindsFeeders.h"
#include "JPFeederTape.h"

#include "JPLengthUnits.h"
#include "JPLocationJson.h"
#include "JPLocationXml.h"
#include "JPOpenPnpIds.h"
#include "JPXmlValues.h"

#include "common/JPlacerLog.h"

#include <j/core/Log.h>

#include "openpnp/JPXmlJson.h"
#include "openpnp/JPXmlWriter.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cmath>
#include <regex>
#include <type_traits>
#include <cstdlib>

inline namespace jf {

namespace {
// The kinds that offer the feed options.
const char* const kFeedOptionsKinds[] = { "ReferenceStripFeeder", "ReferenceTrayFeeder", "ReferencePushPullFeeder",
                                          "ReferenceAutoFeeder", "ReferenceSlotAutoFeeder", "PhotonFeeder",
                                          "BambooFeederAutoVision" };
}

JPFeeder JPFeeder::create(const std::string& className, const std::string& partId) {
    JPFeeder f;
    // A slot feeder: a "SLOT-" id and named by it, holding no part of its own (OpenPnP's).
    const std::string simple = simpleName(className);
    const bool slot = simple == "ReferenceSlotAutoFeeder" || simple == "SlotSchultzFeeder";
    f.m_className = className;
    f.m_id = JPOpenPnpIds::create(slot ? "SLOT-" : "FDR");
    f.m_name = slot ? f.m_id : simple;
    f.m_partId = slot ? std::string() : partId;
    // As a new OpenPnP feeder ends up: made at version 1.0, it is migrated as soon as it is made, its feed retries
    // from its retry count (3), its pick retries 0.
    f.m_pickRetryCount = 0;
    f.ownFrom(JPXmlNode("feeder"));
    return f;
}

namespace {

// The typed kind of `type` (a class's simple name) made from OpenPnP's settings or jplacer's (JSON); false: none.
template <class Own, size_t I = 1>
bool typedFromXml(const std::string& type, const JPXmlNode& own, Own& out) {
    if constexpr (I < std::variant_size_v<Own>) {
        using K = std::variant_alternative_t<I, Own>;
        if (type == JPFeeder::simpleName(K::kClass)) {
            out = K::fromXml(own);
            return true;
        }
        return typedFromXml<Own, I + 1>(type, own, out);
    } else {
        return false;
    }
}

template <class Own, size_t I = 1>
bool typedFromJson(const std::string& type, const JJson& j, Own& out) {
    if constexpr (I < std::variant_size_v<Own>) {
        using K = std::variant_alternative_t<I, Own>;
        if (type == JPFeeder::simpleName(K::kClass)) {
            out = K::fromJson(j[K::kKey]);
            return true;
        }
        return typedFromJson<Own, I + 1>(type, j, out);
    } else {
        return false;
    }
}

} // namespace

void JPFeeder::ownFrom(JPXmlNode own) {
    if (!typedFromXml(typeName(), own, m_own)) m_own = std::move(own);
}

bool JPFeeder::autoTune() const {
    if (typed()) return false;
    const JPXmlNode& n = std::get<JPXmlNode>(m_own);
    const std::string* v = n.get("auto-tune");
    return v ? *v == "true" : true;
}

int JPFeeder::feedCount() const {
    if (const JPTrayFeeder* t = as<JPTrayFeeder>()) return t->feedCount;
    if (typed()) return 0;
    const JPXmlNode* n = std::get_if<JPXmlNode>(&m_own);
    const std::string* v = n ? n->get("feed-count") : nullptr;
    return v && !v->empty() ? std::atoi(v->c_str()) : 0;
}

void JPFeeder::setFeedCount(int n) {
    if (JPTrayFeeder* t = as<JPTrayFeeder>()) t->feedCount = n;
    else if (!typed()) setNumber("feed-count", n);
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
        return m_name + " (Slot: " + (photonSlot ? std::to_string(*photonSlot) : std::string("None")) + ")";
    }
    if (!isSlot()) return m_name;
    return m_name + " (" + (slotLoad ? slotLoad->feederName : std::string("None")) + ")";
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
    m_name = plain;
}

bool JPFeeder::enabled() const {
    const bool on = m_enabled;
    if (isPhoton()) return on && !text("hardware-id").empty() && !m_partId.empty() && photonUnconfigured().empty();
    return isSlot() ? on && slotLoad && !slotLoad->partId.empty() : on;
}

std::string JPFeeder::photonUnconfigured() const {
    const std::string id = text("hardware-id");
    if (!photonSlot) return "Photon Feeder with address " + id + " has no address. Is it inserted?";
    if (!photonSlotLocation) return "The slot at address " + std::to_string(*photonSlot) + " has no location configured.";
    if (!child("offset")) return "Photon Feeder with address " + id + " has no location offset.";
    return {};
}

void JPFeeder::setHardwareId(const std::string& id) {
    if (m_name == typeName()) m_name = id;
    setText("hardware-id", id);
}

std::string JPFeeder::partId() const {
    if (!isSlot()) return m_partId;
    return slotLoad ? slotLoad->partId : std::string();
}

void JPFeeder::setEnabled(bool on) {
    m_enabled = on;
    if (on) m_jobFaults.clear();   // as OpenPnP: turned on, its faults forgotten
}

JPFeeder::Priority JPFeeder::priority() const { return m_priority; }

void JPFeeder::setPriority(Priority p) { m_priority = p; }

bool JPFeeder::supportsFeedOptions() const {
    const std::string t = typeName();
    return std::any_of(std::begin(kFeedOptionsKinds), std::end(kFeedOptionsKinds), [&t](const char* k) { return t == k; });
}

bool JPFeeder::canTakeBackPart() const {
    const std::string t = feedsAs();
    if (t == "ReferenceStripFeeder" || t == "ReferenceTrayFeeder" || t == "ReferenceRotatedTrayFeeder" || t == "BlindsFeeder")
        return feedCount() > 0;
    if (t == "ReferenceAutoFeeder") {
        // A slot's (not typed yet) by OpenPnP's name.
        const JPAutoFeeder* a = as<JPAutoFeeder>();
        return (a ? a->recycleSupport : flag("recycle-support", false)) && feedOptions() != FeedOptions::SkipNext;
    }
    if (t == "ReferencePushPullFeeder" || t == "BambooFeederAutoVision") return feedOptions() != FeedOptions::SkipNext;
    if (t == "PhotonFeeder") return feedOptions() == FeedOptions::Normal;
    if (t == "ReferenceLoosePartFeeder" || t == "AdvancedLoosePartFeeder") return foundPick.has_value();
    return t == "ReferenceHeapFeeder";
}

void JPFeeder::partTakenBack() {
    const std::string t = feedsAs();
    if (t == "ReferenceStripFeeder" || t == "ReferenceTrayFeeder") {
        if (feedOptions() == FeedOptions::Normal) setFeedCount(feedCount() - 1);
    } else if (t == "ReferenceRotatedTrayFeeder" || t == "BlindsFeeder") {
        setFeedCount(feedCount() - 1);
    } else if (t == "ReferenceAutoFeeder" || t == "ReferencePushPullFeeder" || t == "BambooFeederAutoVision" || t == "PhotonFeeder") {
        setFeedOptions(FeedOptions::SkipNext);
    } else if (t == "ReferenceLoosePartFeeder" || t == "AdvancedLoosePartFeeder") {
        foundPick.reset();   // not a second part on the same place
    }
}

JPFeeder::FeedOptions JPFeeder::feedOptions() const { return m_feedOptions; }

void JPFeeder::setFeedOptions(FeedOptions o) { m_feedOptions = o; }

void JPFeeder::setLocation(const JPLocation& l) {
    m_location = l;
    if (isVisionTape()) visionOffset.reset();
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
    if (const JPTrayFeeder* t = as<JPTrayFeeder>()) return t->pickLocation(location());
    if (kind == "ReferenceRotatedTrayFeeder") {
        const int cols = std::max(number("tray-count-cols", 1), 1), rows = std::max(number("tray-count-rows", 1), 1);
        const int count = feedCount();
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
        const long partInCycle = ((long(feedCount()) + cycle - 1) % cycle) + 1;
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
        const int count = std::max(1, feedCount());
        const auto [a, b] = idealLineLocations();
        const JPLength partPitch = lengthOf("part-pitch", JPLength(4, JPLengthUnit::Millimeters));
        const JPLength tapeWidth = lengthOf("tape-width", JPLength(8, JPLengthUnit::Millimeters));
        const double holePitch = 4;   // EIA-481's, in mm
        const JPLocation bb = b.convertToUnits(a.units());
        double pitch = std::hypot(bb.x() - a.x(), bb.y() - a.y());
        const double holes = std::round(pitch / holePitch);
        pitch = holes > 0 ? partPitch.value() / holePitch * pitch / holes : holePitch;
        JPLocation l = pointAlongLine(a, b, JPLength((count - 1) * pitch, partPitch.units()).convertToUnits(a.units()).value());
        // From the hole to the part: across the tape, and along it OpenPnP's Reference Hole To Part Linear
        // (2 mm, EIA-481's P2, unless auto setup found the first part on a hole: 0).
        const double x = JPLength(tapeWidth.convertToUnits(JPLengthUnit::Millimeters).value() / 2 - 0.5, JPLengthUnit::Millimeters)
                             .convertToUnits(l.units()).value();
        const double y = lengthOf("reference-hole-to-part-linear", JPLength(2, JPLengthUnit::Millimeters))
                             .convertToUnits(l.units()).value();
        double angle = b.angleTo(a) - 90;
        const double r = angle * M_PI / 180;
        l = l.add(JPLocation(l.units(), x * std::cos(r) - y * std::sin(r), x * std::sin(r) + y * std::cos(r), 0, 0));
        // A strip not level: picked higher or lower by how far along it is (jplacer's Z Along Strip %).
        l = l.derive(std::nullopt, std::nullopt, JPLength(tapeZ(l), JPLengthUnit::Millimeters).convertToUnits(l.units()).value(),
                     std::nullopt);
        if (flag("standard-eia-481", true)) angle += 90;
        return l.derive(std::nullopt, std::nullopt, std::nullopt, angle + location().rotation());
    }
    return std::nullopt;
}

namespace {

// OpenPnP's names, and jplacer's file's.
JPFeeder::Priority priorityFrom(const std::string& p) {
    return p == "High" ? JPFeeder::Priority::High : p == "Low" ? JPFeeder::Priority::Low : JPFeeder::Priority::Normal;
}

const char* feedOptionsKey(JPFeeder::FeedOptions o) {
    return o == JPFeeder::FeedOptions::SkipNext ? "SkipNext" : o == JPFeeder::FeedOptions::Disable ? "Disable" : "Normal";
}

JPFeeder::FeedOptions feedOptionsFrom(const std::string& o) {
    return o == "SkipNext" ? JPFeeder::FeedOptions::SkipNext
           : o == "Disable" ? JPFeeder::FeedOptions::Disable
                            : JPFeeder::FeedOptions::Normal;
}

} // namespace

JPFeeder JPFeeder::fromXml(const JPXmlElement& e) {
    JPFeeder f;
    JPXmlNode own = JPXmlNode::from(e);
    // What every feeder has taken out of its kind's own.
    auto take = [&own](const char* attribute, const std::string& def = {}) {
        const std::string* v = own.get(attribute);
        const std::string out = v ? *v : def;
        std::erase_if(own.attributes, [attribute](const auto& a) { return a.first == attribute; });
        return out;
    };
    f.m_className = take("class");
    f.m_id = take("id");
    f.m_name = take("name");
    f.m_enabled = take("enabled", "false") == "true";
    f.m_partId = take("part-id");
    f.m_feedRetryCount = std::atoi(take("feed-retry-count", "3").c_str());
    f.m_pickRetryCount = std::atoi(take("pick-retry-count", "3").c_str());
    f.m_priority = priorityFrom(take("priority", "Normal"));
    f.m_feedOptions = feedOptionsFrom(take("feed-options", "Normal"));
    // As OpenPnP brings a feeder of its version 1.0 to 1.1: its retry count is its feed retries, no pick retries.
    const std::string retries = take("retry-count", "3");
    if (std::strtod(take("version", "1.0").c_str(), nullptr) < 1.1) {
        f.m_feedRetryCount = std::atoi(retries.c_str());
        f.m_pickRetryCount = 0;
    }
    if (const JPXmlNode* l = own.child("location")) {
        f.m_location = JPLocationXml::from(*l);
        std::erase_if(own.children, [](const JPXmlNode& c) { return c.name == "location"; });
    }
    f.ownFrom(std::move(own));
    if (f.typeName() == "ReferenceStripFeeder" && f.text("standard-eia-481").empty()) {
        // OpenPnP's angleNorm(rotation - 90, 180): within -180 (exclusive) .. 180.
        double r = f.m_location.rotation() - 90;
        while (r > 180) r -= 360;
        while (r <= -180) r += 360;
        f.m_location = f.m_location.derive(std::nullopt, std::nullopt, std::nullopt, r);
        f.setFlag("standard-eia-481", true);
    }
    return f;
}

JPFeeder JPFeeder::fromJson(const JJson& j) {
    JPFeeder f;
    f.m_className = j["class"].str();
    f.m_id = j["id"].str();
    f.m_name = j["name"].str();
    f.m_enabled = j["enabled"].boolean(false);
    f.m_partId = j["part"].str();
    f.m_feedRetryCount = int(j["feedRetryCount"].number(3));
    f.m_pickRetryCount = int(j["pickRetryCount"].number(3));
    f.m_priority = priorityFrom(j["priority"].str());
    f.m_feedOptions = feedOptionsFrom(j["feedOptions"].str());
    f.m_location = JPLocationJson::from(j["location"]);
    if (!typedFromJson(f.typeName(), j, f.m_own)) f.m_own = JPXmlJson::node("feeder", j["openpnp"]);
    return f;
}

JJson JPFeeder::toJson() const {
    JJson j = JJson::object();
    j["class"] = m_className;
    j["id"] = m_id;
    j["name"] = m_name;
    j["enabled"] = m_enabled;
    j["part"] = m_partId;
    j["feedRetryCount"] = m_feedRetryCount;
    j["pickRetryCount"] = m_pickRetryCount;
    j["priority"] = priorityName(m_priority);
    j["feedOptions"] = feedOptionsKey(m_feedOptions);
    j["location"] = JPLocationJson::to(m_location);
    std::visit(
        [&j](const auto& own) {
            using K = std::decay_t<decltype(own)>;
            if constexpr (std::is_same_v<K, JPXmlNode>) j["openpnp"] = JPXmlJson::from(own);
            else j[K::kKey] = own.toJson();
        },
        m_own);
    return j;
}

bool JPFeeder::partHeightAbovePickLocation() const {
    const std::string kind = typeName();
    return kind == "ReferenceLoosePartFeeder" || kind == "AdvancedLoosePartFeeder";
}

bool JPFeeder::feed(std::string& why, bool* empty) {
    if (empty) *empty = false;
    const std::string kind = typeName();
    if (JPTrayFeeder* t = as<JPTrayFeeder>()) {
        if (t->empty()) {
            why = "Feeder: " + name() + " (" + partId() + ") - tray empty.";
            if (empty) *empty = true;
            return false;
        }
        if (feedOptions() == FeedOptions::Normal || t->feedCount == 0) ++t->feedCount;
        if (feedOptions() == FeedOptions::SkipNext) setFeedOptions(FeedOptions::Normal);
        return true;
    }
    if (kind == "ReferenceRotatedTrayFeeder") {
        const int cols = std::max(number("tray-count-cols", 1), 1), rows = std::max(number("tray-count-rows", 1), 1);
        if (feedCount() >= cols * rows) {
            why = name() + " (" + partId() + ") is empty.";
            if (empty) *empty = true;
            return false;
        }
        setFeedCount(feedCount() + 1);
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
    if (kind != "ReferenceStripFeeder") {
        why = "Feeding a " + kind + " is not available yet.";
        return false;
    }
    if (feedOptions() == FeedOptions::Normal || feedCount() == 0) setFeedCount(feedCount() + 1);
    {
        const int most = number("max-feed-count");
        if (most > 0 && feedCount() > most) {
            why = "Tried to feed part: " + partId() + "  Feeder " + name() + " empty.";
            if (empty) *empty = true;
            return false;
        }
    }
    // A strip's vision, as OpenPnP's: the first hole when the first pick is
    // mid-strip, then the hole fed (not when its pick is repeated).
    if (flag("vision-enabled", false)) {
        const int count = feedCount();
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

double JPFeeder::tapeZ(const JPLocation& at) const {
    const JPLocation ref = locationOf("reference-hole-location").convertToUnits(JPLengthUnit::Millimeters);
    const JPLocation last = locationOf("last-hole-location").convertToUnits(JPLengthUnit::Millimeters);
    const JPLocation l = at.convertToUnits(JPLengthUnit::Millimeters);
    // The strip's slope (OpenPnP's strip is level, at the reference hole's Z).
    const double slope = real("z-along-strip-percent", 0);
    const double lx = last.x() - ref.x(), ly = last.y() - ref.y(), len = std::hypot(lx, ly);
    if (slope == 0 || len == 0) return ref.z();
    return ref.z() + slope / 100 * ((l.x() - ref.x()) * lx + (l.y() - ref.y()) * ly) / len;
}

JPLocation JPFeeder::visionView(const JPLocation& hole) const {
    const auto [a, b] = idealLineLocations();
    const JPLocation aa = a.convertToUnits(JPLengthUnit::Millimeters), bb = b.convertToUnits(JPLengthUnit::Millimeters);
    const JPLocation h = hole.convertToUnits(JPLengthUnit::Millimeters);
    const double len = std::hypot(bb.x() - aa.x(), bb.y() - aa.y());
    if (len == 0) return h;
    // Across the tape to its parts, as the pick (OpenPnP's hole to part lateral, turned with the tape).
    const double across = mm(lengthOf("tape-width", JPLength(8, JPLengthUnit::Millimeters))) / 2 - 0.5;
    const double r = (bb.angleTo(aa) - 90) * M_PI / 180;
    const double half = mm(holePitch()) / 2;
    return JPLocation(JPLengthUnit::Millimeters, h.x() + across * std::cos(r) + (bb.x() - aa.x()) / len * half,
                      h.y() + across * std::sin(r) + (bb.y() - aa.y()) / len * half, h.z(), 0);
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
    if (enabled() && faultLimit > 0 && faults >= faultLimit) m_enabled = false;
}

const JPXmlNode* JPFeeder::openPnp(const std::string& setting) const {
    if (const JPXmlNode* n = std::get_if<JPXmlNode>(&m_own)) return n;
    // A typed kind's settings are its own: one asked for by OpenPnP's name is a mistake here, not a default.
    JLOGC(JPlacerLog::kFeeders, JLogLevel::Error) << "feeder " << m_id << " (" << typeName() << "): its setting \"" << setting
                                                  << "\" was asked for by OpenPnP's name, which its kind keeps as its own";
    return nullptr;
}

JPXmlNode* JPFeeder::openPnp(const std::string& setting) {
    return const_cast<JPXmlNode*>(std::as_const(*this).openPnp(setting));
}

const std::string* JPFeeder::attribute(const std::string& name) const {
    const JPXmlNode* n = openPnp(name);
    return n ? n->get(name) : nullptr;
}

void JPFeeder::setAttribute(const std::string& name, const std::string& value) {
    if (JPXmlNode* n = openPnp(name)) n->set(name, value);
}

const JPXmlNode* JPFeeder::child(const std::string& element) const {
    const JPXmlNode* n = openPnp(element);
    return n ? n->child(element) : nullptr;
}

void JPFeeder::putChild(JPXmlNode node) {
    JPXmlNode* n = openPnp(node.name);
    if (!n) return;
    if (JPXmlNode* c = n->child(node.name)) *c = std::move(node);
    else n->add(std::move(node));
}

std::string JPFeeder::text(const std::string& attribute, const std::string& def) const {
    const std::string* v = this->attribute(attribute);
    return v ? *v : def;
}

void JPFeeder::setText(const std::string& attribute, const std::string& value) { setAttribute(attribute, value); }

bool JPFeeder::hasAttribute(const std::string& attribute) const { return this->attribute(attribute) != nullptr; }

void JPFeeder::removeAttribute(const std::string& attribute) {
    if (JPXmlNode* n = openPnp(attribute))
        std::erase_if(n->attributes, [&attribute](const auto& a) { return a.first == attribute; });
}

int JPFeeder::number(const std::string& attribute, int def) const {
    const std::string* v = this->attribute(attribute);
    return v && !v->empty() ? std::atoi(v->c_str()) : def;
}

void JPFeeder::setNumber(const std::string& attribute, int value) { setAttribute(attribute, std::to_string(value)); }

double JPFeeder::real(const std::string& attribute, double def) const {
    const std::string* v = this->attribute(attribute);
    return v && !v->empty() ? std::strtod(v->c_str(), nullptr) : def;
}

void JPFeeder::setReal(const std::string& attribute, double value) { setAttribute(attribute, JPXmlWriter::number(value)); }

bool JPFeeder::flag(const std::string& attribute, bool def) const {
    const std::string* v = this->attribute(attribute);
    return v ? *v == "true" : def;
}

void JPFeeder::setFlag(const std::string& attribute, bool on) { setAttribute(attribute, on ? "true" : "false"); }

void JPFeeder::setChild(JPXmlNode node) { putChild(std::move(node)); }

void JPFeeder::removeChild(const std::string& element) {
    if (JPXmlNode* n = openPnp(element))
        std::erase_if(n->children, [&element](const JPXmlNode& c) { return c.name == element; });
}

JPLocation JPFeeder::locationOf(const std::string& element) const {
    const JPXmlNode* c = child(element);
    return c ? JPLocationXml::from(*c) : JPLocation(JPLengthUnit::Millimeters);
}

void JPFeeder::setLocationOf(const std::string& element, const JPLocation& l) {
    putChild(JPLocationXml::to(element, l));
    // As OpenPnP's setHole1Location and setHole2Location: the calibration reset.
    if (isVisionTape() && (element == "hole-1-location" || element == "hole-2-location"))
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
    const JPXmlNode* n = openPnp(path + "/" + attribute);
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
    JPXmlNode* n = openPnp(path + "/" + attribute);
    if (!n) return;
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
    putChild(std::move(pipeline));
}

std::string JPFeeder::childText(const std::string& element, const std::string& def) const {
    const JPXmlNode* c = child(element);
    return c ? c->text : def;
}

void JPFeeder::setChildText(const std::string& element, const std::string& value) {
    JPXmlNode fresh(element);
    if (const JPXmlNode* c = child(element)) fresh = *c;
    fresh.text = value;
    putChild(std::move(fresh));
}

JPLength JPFeeder::lengthOf(const std::string& element, const JPLength& def) const {
    const JPXmlNode* c = child(element);
    if (!c || !c->get("value")) return def;
    JPLengthUnit u = JPLengthUnit::Millimeters;
    if (const std::string* units = c->get("units")) JPLengthUnits::fromName(*units, u);
    return JPLength(std::strtod(c->get("value")->c_str(), nullptr), u);
}

void JPFeeder::setLengthOf(const std::string& element, const JPLength& l) {
    JPXmlNode fresh(element);
    fresh.attr("value", JPXmlWriter::number(l.value())).attr("units", JPLengthUnits::name(l.units()));
    putChild(std::move(fresh));
}

} // inline namespace jf
