// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "openpnp/JPXmlNode.h"

#include <j/config/Json.h>

#include <optional>

inline namespace jf {

// A loose part feeder (OpenPnP's ReferenceLoosePartFeeder): parts lying loose on a surface, each found by its
// vision pipeline (OpenPnP's CvPipeline, as OpenPnP keeps it; none: the default one) and picked where it lies.
class JPLoosePartFeeder {
public:
    static constexpr const char* kClass = "org.openpnp.machine.reference.feeder.ReferenceLoosePartFeeder";
    static constexpr const char* kKey = "loosePart";

    std::optional<JPXmlNode> pipeline;

    // OpenPnP's settings of it (its <feeder>'s own, what every feeder has taken out).
    static JPLoosePartFeeder fromXml(const JPXmlNode& own);
    // As jplacer's feeders.json keeps it: {"pipeline"} (in OpenPnP's shape, JPXmlJson).
    static JPLoosePartFeeder fromJson(const JJson& j);
    JJson toJson() const;
};

} // inline namespace jf
