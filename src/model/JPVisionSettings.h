// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPLocation.h"

#include "openpnp/JPXmlElement.h"
#include "openpnp/JPXmlNode.h"

#include <string>

inline namespace jf {

// One of OpenPnP's vision settings (vision-settings.xml): its id, name,
// whether it is for bottom vision or fiducials and enabled, and the rest as
// OpenPnP wrote it (its pipeline, its own settings), written back as read.
class JPVisionSettings {
public:
    enum class Kind { Bottom, Fiducial, Other };

    // OpenPnP's AbstractVisionSettings ids: the stock settings, and the machine's defaults.
    static constexpr const char* kStockBottomId   = "BVS_Stock";
    static constexpr const char* kStockFiducialId = "FVS_Stock";
    static constexpr const char* kDefaultBottomId   = "BVS_Default";
    static constexpr const char* kDefaultFiducialId = "FVS_Default";

    std::string id;
    std::string name;
    Kind        kind = Kind::Other;
    bool        enabled = true;

    static JPVisionSettings fromXml(const JPXmlElement& e);
    JPXmlNode toXml() const;
    // A new one of `kind`, as OpenPnP's New Settings makes it: named after its class.
    static JPVisionSettings create(Kind kind, const std::string& id);

    // OpenPnP's isStockSetting: its id says "Stock" (not to be renamed or deleted).
    bool isStock() const { return id.find("Stock") != std::string::npos; }
    std::string className() const;

    // Its own values, by their XML names.
    std::string text(const std::string& attribute, const std::string& def = {}) const;
    void        setText(const std::string& attribute, const std::string& value);
    int         number(const std::string& attribute, int def = 0) const;
    double      real(const std::string& attribute, double def = 0) const;
    bool        flag(const std::string& attribute, bool def = false) const;
    // A length element (<max-linear-offset value= units=>), in millimetres.
    double      lengthMm(const std::string& element, double def) const;
    void        setLengthMm(const std::string& element, double mm);
    JPLocation  locationOf(const std::string& element) const;
    void        setLocationOf(const std::string& element, const JPLocation& l);
    // Its pipeline as OpenPnP wrote it (<cv-pipeline>), or null; and one
    // put in its place (first, as OpenPnP writes it).
    const JPXmlNode* pipeline() const { return m_node.child("cv-pipeline"); }
    void             setPipeline(JPXmlNode pipeline);
    // The values it gives its pipeline's parameters (<pipeline-parameter-assignments>), or null; and new ones.
    const JPXmlNode* parameterAssignments() const { return m_node.child("pipeline-parameter-assignments"); }
    void             setParameterAssignments(JPXmlNode assignments);

private:
    JPXmlNode m_node;
};

} // inline namespace jf
