// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "openpnp/JPXmlNode.h"

#include <j/config/Json.h>

#include <optional>

inline namespace jf {

// An advanced loose part feeder (OpenPnP's AdvancedLoosePartFeeder): loose parts each found by its feed pipeline,
// with a training pipeline that makes the template the feed pipeline looks for (each OpenPnP's CvPipeline, as
// OpenPnP keeps it; none: the default one).
class JPAdvancedLoosePartFeeder {
public:
    static constexpr const char* kClass = "org.openpnp.machine.reference.feeder.AdvancedLoosePartFeeder";
    static constexpr const char* kKey = "advancedLoosePart";

    std::optional<JPXmlNode> pipeline, trainingPipeline;

    // OpenPnP's settings of it (its <feeder>'s own, what every feeder has taken out).
    static JPAdvancedLoosePartFeeder fromXml(const JPXmlNode& own);
    // As jplacer's feeders.json keeps it: {"pipeline", "trainingPipeline"} (in OpenPnP's shape, JPXmlJson).
    static JPAdvancedLoosePartFeeder fromJson(const JJson& j);
    JJson toJson() const;
};

} // inline namespace jf
