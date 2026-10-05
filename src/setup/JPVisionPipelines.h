// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "model/JPVisionSettings.h"
#include "pipeline/JPPipeline.h"
#include "pipeline/JPPipelineAssignments.h"

#include <string>

inline namespace jf {

// A vision setting's pipeline, as OpenPnP's PipelineControls work it: the
// one it holds (OpenPnP's stock default for its kind when it holds none),
// put back to the default, copied and pasted as OpenPnP's text, and the
// values it assigns to the pipeline's parameters.
class JPVisionPipelines {
public:
    static JPPipeline of(const JPVisionSettings& settings);
    static void       set(JPVisionSettings& settings, const JPPipeline& pipeline);
    // OpenPnP's resetPipeline: the machine's default setting's pipeline, or
    // (this being the machine's default) the stock one.
    static void       reset(JPVisionSettings& settings, const JPVisionSettings* machineDefault);
    static std::string copy(const JPVisionSettings& settings);
    // False (and why) for text that is not a pipeline.
    static bool       paste(JPVisionSettings& settings, const std::string& text, std::string& why);

    static JPPipelineAssignments::Map assignments(const JPVisionSettings& settings);
    static void                       assign(JPVisionSettings& settings, const std::string& parameter, const JPPipelineValue& value);
};

} // inline namespace jf
