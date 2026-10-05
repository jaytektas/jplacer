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
    // ReferenceBottomVision-DefaultPipeline.xml (createStockPipeline("Default")).
    static const std::string& bottomVision();
    // ReferenceFiducialLocator-DefaultPipeline.xml (createStockPipeline("Default")).
    static const std::string& fiducialLocator();
};

} // inline namespace jf
