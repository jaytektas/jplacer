// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include <string>

inline namespace jf {

// OpenPnP's default pipelines, as its resources hold them: what Reset
// Pipeline puts back, and what a setting without a pipeline starts from.
class JPDefaultPipelines {
public:
    // ReferenceStripFeeder-DefaultPipeline.xml.
    static const std::string& stripFeeder();
    // ReferenceLoosePartFeeder-DefaultPipeline.xml.
    static const std::string& loosePartFeeder();
    // AdvancedLoosePartFeeder-DefaultPipeline.xml.
    static const std::string& advancedLoosePartFeeder();
    // AdvancedLoosePartFeeder-DefaultTrainingPipeline.xml.
    static const std::string& advancedLoosePartFeederTraining();
    // FeederVisionHelper-CircularSymmetry-Pipeline.xml and -ColorKeyed-Pipeline.xml:
    // a sprocket hole tape feeder's, by its Vision Type.
    static const std::string& feederVisionCircularSymmetry();
    static const std::string& feederVisionColorKeyed();
    // HeapFeeder-<type>-<colour>-Pipeline.xml: a heap feeder's (type "Part",
    // "Training") or its drop box's ("DropBox"), by the drop box's colour
    // ("GREEN", "WHITE", "BLACK"); null for another.
    static const std::string* heapFeeder(const std::string& type, const std::string& colour);
    // ReferenceBottomVision-DefaultPipeline.xml (createStockPipeline("Default")).
    static const std::string& bottomVision();
    // ReferenceFiducialLocator-DefaultPipeline.xml (createStockPipeline("Default")).
    static const std::string& fiducialLocator();
};

} // inline namespace jf
