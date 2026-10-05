// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "model/JPFeeder.h"
#include "pipeline/JPPipeline.h"

#include <optional>

inline namespace jf {

// A feeder's vision pipeline, as OpenPnP's feeders keep theirs: the one it
// holds (its kind's default when it holds none), the default put back, and
// what the feeder sets on it before it runs (sizes in pixels through the
// camera's scale).
class JPFeederPipelines {
public:
    // Its pipeline; none for a kind without one.
    static std::optional<JPPipeline> of(const JPFeeder& feeder);
    // Its kind's default back; false for a kind without one.
    static bool reset(JPFeeder& feeder);
    // What OpenPnP's editor sets on it (ReferenceStripFeederConfigurationWizard's
    // getCvPipeline), the camera's scale and size in its context.
    static void configureForEditing(const JPFeeder& feeder, JPPipeline& pipeline);
};

} // inline namespace jf
