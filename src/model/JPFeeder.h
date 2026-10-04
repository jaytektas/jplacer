// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPLength.h"
#include "JPLocation.h"

#include "openpnp/JPXmlElement.h"
#include "openpnp/JPXmlNode.h"

#include <deque>
#include <optional>
#include <string>
#include <vector>

inline namespace jf {

// A feeder, as OpenPnP keeps one in its machine's <feeders>: its class
// (ReferenceStripFeeder, ReferenceTrayFeeder…), id, name, enabled, part,
// retries, priority and feed option, where it is, and what its kind adds
// (a strip's hole locations and pitches, a tray's counts, a vision
// pipeline). The element is kept as read and changed in place, so a feeder
// is written back exactly as OpenPnP wrote it, whatever its kind.
class JPFeeder {
public:
    // OpenPnP's Feeder.Priority and ReferenceFeeder.FeedOptions, in their order.
    enum class Priority { High, Normal, Low };
    enum class FeedOptions { Normal, SkipNext, Disable };

    explicit JPFeeder(JPXmlNode element) : m_node(std::move(element)) {}
    // A new feeder of an OpenPnP class (OpenPnP's AbstractFeeder): a new id
    // ("FDR" and the time in nanoseconds, in hex), named after its class,
    // turned off, holding `partId`.
    static JPFeeder create(const std::string& className, const std::string& partId);
    // The classes a feeder can be, as OpenPnP offers them for a new one
    // (ReferenceMachine.getCompatibleFeederClasses), in its order.
    static const std::vector<std::string>& classNames();
    // A class's simple name ("org.openpnp…ReferenceStripFeeder": "ReferenceStripFeeder").
    static std::string simpleName(const std::string& className);
    static JPFeeder fromXml(const JPXmlElement& e) { return JPFeeder(JPXmlNode::from(e)); }
    const JPXmlNode& toXml() const { return m_node; }

    std::string className() const;
    // The class's simple name, as OpenPnP's Type column shows it.
    std::string typeName() const;

    std::string id() const { return text("id"); }
    // A slot feeder's name has what is loaded in it after it, as OpenPnP
    // shows it ("SLOT-1 (Feeder 7)", "(None)"); set, that is taken off again.
    std::string name() const;
    void        setName(const std::string& n);
    // A slot feeder is enabled only with a feeder holding a part loaded in it.
    bool        enabled() const;
    void        setEnabled(bool on);
    // A slot feeder's part is the one its loaded feeder holds (none without one).
    std::string partId() const;
    void        setPartId(const std::string& id) { setText("part-id", id); }

    // A SLOT FEEDER (OpenPnP's ReferenceSlotAutoFeeder, SlotSchultzFeeder):
    // a place feeders of a bank (JPSlotBanks) are loaded into, its bank and
    // the feeder loaded named by its bank-id and feeder-id. What is loaded,
    // as the configuration last found it (JPConfiguration::resolveSlots).
    bool isSlot() const;
    // The kind a slot feeder feeds as (ReferenceAutoFeeder, SchultzFeeder); else its own.
    std::string feedsAs() const;
    struct SlotLoad {
        std::string feederName, partId;
        JPLocation  offsets { JPLengthUnit::Millimeters };
    };
    std::optional<SlotLoad> slotLoad;
    int         feedRetryCount() const { return number("feed-retry-count", 3); }
    void        setFeedRetryCount(int n) { setNumber("feed-retry-count", n); }
    int         pickRetryCount() const { return number("pick-retry-count", 3); }
    void        setPickRetryCount(int n) { setNumber("pick-retry-count", n); }
    Priority    priority() const;
    void        setPriority(Priority p);
    // Whether its kind offers the feed options (OpenPnP's supportsFeedOptions).
    bool        supportsFeedOptions() const;
    FeedOptions feedOptions() const;
    void        setFeedOptions(FeedOptions o);
    JPLocation  location() const { return locationOf("location"); }
    void        setLocation(const JPLocation& l) { setLocationOf("location", l); }

    // Where the next part is picked, for the kinds jplacer works out
    // (OpenPnP's getPickLocation); none for the others.
    std::optional<JPLocation> pickLocation() const;
    // A feed (OpenPnP's feed, but for a strip's vision check): the count
    // moved on as the feed option says. False, and why, when it cannot be
    // fed; `empty` says when that is because it is empty (OpenPnP's
    // FeederEmptyException).
    bool feed(std::string& why, bool* empty = nullptr);
    // A strip's vision (OpenPnP's updateVisionOffsets): the holes the last
    // feed wants looked at (by feed count: the first hole, when picking
    // starts mid-strip; the one fed), each taken once.
    std::vector<int> takeVisionChecks();
    // An auto feeder's feed: whether the last feed wants its feed actuator
    // actuated (a normal feed, not one repeated or disabled), taken once.
    bool takeFeedActuation();
    // Where the hole for feed count `n` is expected; none when vision need not
    // look (vision off, or near enough to the last found within the
    // extrapolation distance).
    std::optional<JPLocation> visionExpected(int n) const;
    // Where it was found: the line its parts lie on follows it.
    void setVisionFound(int n, const JPLocation& found);
    JPLength holeDiameter() const { return lengthOf("hole-diameter", JPLength(1.5, JPLengthUnit::Millimeters)); }
    JPLength holePitch() const { return lengthOf("hole-pitch", JPLength(4, JPLengthUnit::Millimeters)); }
    // A strip's holes as its vision last found them (none: as set), and the
    // line its parts lie on.
    std::optional<JPLocation> visionLocation, visionLocationReference;
    std::pair<JPLocation, JPLocation> idealLineLocations() const;

    // A drag or lever feeder (OpenPnP's ReferenceDragFeeder and
    // ReferenceLeverFeeder), while jplacer runs: how far the location is
    // from where its vision last found the template (a drag feeder with
    // none looks before its next drag), the pick's step along the tape for
    // the next of the parts one feed brings (2 mm pitch), and how many of
    // those are left.
    std::optional<JPLocation> templateOffset, nextPartPick;
    int                       partsFed = 0;
    // OpenPnP's resetVisionOffsets.
    void resetVisionOffsets();
    // Its template image's file (OpenPnP's resource file of the vision's
    // template-image-name in `configDirectory`); empty when it has none.
    std::string templatePath(const std::string& configDirectory) const;
    // Where OpenPnP keeps the template images of a feeder class (its
    // Vision's resource directory) in its configuration directory.
    static std::string templateDirectory(const std::string& configDirectory, const std::string& className);

    static const char* priorityName(Priority p);
    // As OpenPnP shows them: "Normal feed", "Skip next feed", "Disable feed".
    static const char* feedOptionsName(FeedOptions o);

    // The job's faults on it, newest first, as OpenPnP's Faults column shows
    // them ("X-X"; empty when none); kept while jplacer runs.
    std::string summariseJobFaults() const;
    void        recordJobSuccess(int windowSize);
    void        recordJobFault(int faultLimit, int windowSize);

    // Its kind's own values, by their XML names.
    std::string text(const std::string& attribute, const std::string& def = {}) const;
    void        setText(const std::string& attribute, const std::string& value);
    int         number(const std::string& attribute, int def = 0) const;
    void        setNumber(const std::string& attribute, int value);
    double      real(const std::string& attribute, double def = 0) const;
    void        setReal(const std::string& attribute, double value);
    bool        flag(const std::string& attribute, bool def = false) const;
    void        setFlag(const std::string& attribute, bool on);
    JPLocation  locationOf(const std::string& element) const;
    void        setLocationOf(const std::string& element, const JPLocation& l);
    // A child element's text (OpenPnP's <parallax-angle>0.0</parallax-angle>).
    std::string childText(const std::string& element, const std::string& def = {}) const;
    void        setChildText(const std::string& element, const std::string& value);
    // An attribute of a child element, by its path ("vision/area-of-interest");
    // set, the elements are made when missing.
    std::string attributeAt(const std::string& path, const std::string& attribute, const std::string& def = {}) const;
    void        setAttributeAt(const std::string& path, const std::string& attribute, const std::string& value);
    JPLength    lengthOf(const std::string& element, const JPLength& def) const;
    void        setLengthOf(const std::string& element, const JPLength& l);

private:
    void addJobFault(bool fault, int windowSize);

    JPXmlNode        m_node;
    std::vector<int> m_visionChecks;
    bool             m_actuate = false;
    std::deque<bool> m_jobFaults;
};

} // inline namespace jf
