// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include <opencv2/core.hpp>

#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "JPPipelineModel.h"

inline namespace jf {

class JPPipeline;
class JPPipelineStage;

// What a kind of pipeline stage is (one of OpenPnP's CvStage classes): its
// class, category and description (its @Stage), its settings in the order
// OpenPnP declares them (each its XML name, kind, default and description,
// its @Property), and what it does with the pipeline.
struct JPStageType {
    enum class Kind { Integer, Number, Flag, Text, Choice, Color, StageName };
    struct Property {
        std::string              attribute;   // its XML name ("kernel-size"); a Color: its element's
        Kind                     kind = Kind::Text;
        std::string              def;         // as written ("3", "true", "Bgr2Gray"); a Color: "r,g,b,a"
        std::string              description;
        std::vector<std::string> choices;     // Choice
    };
    // What a stage gives back (OpenPnP's Result): a new working image (empty:
    // the working image as it is now), its colour space (empty: unchanged), and
    // what it found.
    struct Output {
        cv::Mat                     image;
        std::optional<std::string>  colorSpace;
        JPPipelineModel             model;
    };
    // The stage may set its own settings (a MaskHsv found its limits itself).
    using Process = std::function<Output(JPPipeline& pipeline, JPPipelineStage& stage)>;

    std::string           className;     // "org.openpnp.vision.pipeline.stages.BlurGaussian"
    std::string           category;      // "Image Processing"
    std::string           description;
    std::vector<Property> properties;
    Process               process;

    std::string typeName() const;
    const Property* property(const std::string& attribute) const;
};

} // inline namespace jf
