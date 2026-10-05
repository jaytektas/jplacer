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
    // A push-pull feeder's OCR (OpenPnP's setupOcr): its OCR region the
    // pipeline's region of interest, its font and size, and every character
    // of the part ids its alphabet; or OCR switched off (an empty alphabet,
    // the stages' own region, font and size).
    static void setupOcr(const JPConfiguration& config, const JPFeeder& feeder, JPPipeline& pipeline);
    static void disableOcr(JPPipeline& pipeline);
    // A blinds feeder's OCR as `action` asks (None: off): its label's corners,
    // about the camera at `cameraAt`, the region of interest, rectified.
    static void setupBlindsOcr(const JPConfiguration& config, const JPFeeder& feeder, JPPipeline& pipeline, const JPLocation& cameraAt,
                               const std::string& action);
};

} // inline namespace jf
