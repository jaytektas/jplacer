// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPStageType.h"

#include <string>
#include <vector>

inline namespace jf {

// Every kind of pipeline stage jplacer runs (OpenPnP's stages), each group
// made in a file of its own.
class JPStageRegistry {
public:
    static const JPStageRegistry& instance();
    // By OpenPnP class ("org.openpnp.vision.pipeline.stages.BlurGaussian") or simple name; none: unknown.
    const JPStageType* find(const std::string& className) const;
    const std::vector<JPStageType>& types() const { return m_types; }

    // The groups.
    static void addImageStages(std::vector<JPStageType>& types);
    static void addFilterStages(std::vector<JPStageType>& types);
    static void addDetectStages(std::vector<JPStageType>& types);
    static void addModelStages(std::vector<JPStageType>& types);
    static void addDrawStages(std::vector<JPStageType>& types);
    static void addParameterStages(std::vector<JPStageType>& types);
    static void addMatchStages(std::vector<JPStageType>& types);
    static void addTemplateStages(std::vector<JPStageType>& types);

private:
    JPStageRegistry();
    std::vector<JPStageType> m_types;
};

} // inline namespace jf
