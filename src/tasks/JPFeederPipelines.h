// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "model/JPConfiguration.h"
#include "model/JPFeeder.h"
#include "pipeline/JPPipeline.h"

#include <optional>
#include <string>

inline namespace jf {

// A feeder's vision pipelines, as OpenPnP's feeders keep theirs: the one it
// holds under `element` ("pipeline"; an advanced loose part feeder's
// "training-pipeline" too), its kind's default when it holds none, the
// default put back, and what the feeder sets on it before it runs.
class JPFeederPipelines {
public:
    // Its pipeline; none for a kind (or element) without one.
    static std::optional<JPPipeline> of(const JPFeeder& feeder, const std::string& element = "pipeline");
    // Its kind's default back; false for a kind without one.
    static bool reset(JPFeeder& feeder, const std::string& element = "pipeline");
    // What OpenPnP's editor sets on it (a strip feeder's sizes in pixels, a
    // loose part feeder's part), the camera's scale and size in its context.
    static void configureForEditing(const JPConfiguration& config, const JPFeeder& feeder, JPPipeline& pipeline);
};

} // inline namespace jf
