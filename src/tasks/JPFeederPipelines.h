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
    // Its pipeline; none for a kind (or element) without one. A heap feeder's
    // ("feeder-pipeline", "training-pipeline") default by its drop box's colour (`boxes`).
    static std::optional<JPPipeline> of(const JPFeeder& feeder, const std::string& element = "pipeline",
                                        const JPDropBoxes* boxes = nullptr);
    // Its kind's default back; false for a kind without one.
    static bool reset(JPFeeder& feeder, const std::string& element = "pipeline", const JPDropBoxes* boxes = nullptr);
    // The drop box a heap feeder uses (its drop-box-id, else the last box, as OpenPnP's getDropBox).
    static std::string dropBoxOf(const JPFeeder& feeder, const JPDropBoxes& boxes);
    // A drop box's parts pipeline (its colour's default when it keeps none), and its default back.
    static std::optional<JPPipeline> ofDropBox(const JPDropBoxes& boxes, const std::string& boxId);
    static bool                      resetDropBox(JPDropBoxes& boxes, const std::string& boxId);
    // What OpenPnP's editor sets on it (a strip feeder's sizes in pixels, a
    // loose part feeder's part), the camera's scale and size in its context.
    static void configureForEditing(const JPConfiguration& config, const JPFeeder& feeder, JPPipeline& pipeline);
    // OpenPnP's AbstractPandaplacerVisionFeeder.getCvPipeline: the sprocket
    // holes' size, and how far from the camera's centre they are looked for:
    // for Auto Setup the whole picture (`width` x `height` pixels at the scale
    // given), else half the distance between the feeder's holes and a pitch.
    static void configureTape(const JPFeeder& feeder, JPPipeline& pipeline, bool autoSetup, int width, int height,
                              double mmPerPixelX, double mmPerPixelY);
};

} // inline namespace jf
