// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPLocation.h"

#include "openpnp/JPXmlElement.h"
#include "openpnp/JPXmlNode.h"

#include <map>
#include <optional>
#include <string>
#include <vector>

inline namespace jf {

// The drop boxes heap feeders drop their parts into to look at them (OpenPnP's
// ReferenceHeapFeeder.DropBox), kept as OpenPnP keeps them: the machine
// property "ReferenceHeapFeeder.dropBoxes", an <entry> of machine.xml's
// <properties>, each box an id, a name, the dummy part that moves parts of
// unknown origin, its parts pipeline, its centre (X, Y) and bottom (Z), and
// where parts are dropped into it. The entry is kept as read and changed in
// place. There is always a box ("Green", as OpenPnP makes the first).
class JPDropBoxes {
public:
    static constexpr const char* kKey = "ReferenceHeapFeeder.dropBoxes";
    struct Box {
        std::string id, name, dummyPartId;
        JPLocation  centerBottom { JPLengthUnit::Millimeters };
        JPLocation  drop { JPLengthUnit::Millimeters };
    };

    JPDropBoxes();
    // From machine.xml's <entry> (its <string> the key); none for another property.
    static std::optional<JPDropBoxes> fromXml(const JPXmlElement& entry);
    const JPXmlNode& toXml() const { return m_entry; }

    std::vector<Box>   boxes() const;
    std::optional<Box> box(const std::string& id) const;
    // A new box (OpenPnP's "DropBox-" id, named by it); its id.
    std::string add();
    // False, and why, for the only box.
    bool remove(const std::string& id, std::string& why);
    void setName(const std::string& id, const std::string& name);
    void setDummyPart(const std::string& id, const std::string& partId);
    void setCenterBottom(const std::string& id, const JPLocation& l);
    void setDrop(const std::string& id, const JPLocation& l);
    // Its parts pipeline (OpenPnP's <part-pipeline>); null when none is kept.
    const JPXmlNode* partPipeline(const std::string& id) const;
    void             setPartPipeline(const std::string& id, JPXmlNode pipeline);
    // The colour its default pipelines are for, from its name ("GREEN", "WHITE" or "BLACK"; else GREEN).
    static std::string colour(const std::string& name);

    // While jplacer runs: the heap feeder whose parts are in each box (by box id; none: unknown or empty).
    std::map<std::string, std::string> lastHeap;

private:
    JPXmlNode*       boxesNode();
    const JPXmlNode* boxesNode() const;
    JPXmlNode*       boxNode(const std::string& id);
    const JPXmlNode* boxNode(const std::string& id) const;

    JPXmlNode m_entry { "entry" };
};

} // inline namespace jf
