// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "model/JPConfiguration.h"

#include <functional>
#include <string>

inline namespace jf {

// A vision setting's pipeline buttons, the same wherever its page is shown
// (the Vision tab, a part's, a package's), as OpenPnP's PipelineControls:
// Edit (the editor), Reset and Paste (each asked first), Copy; and a
// parameter's slider moved (its effect shown).
class JPVisionPipelineActions {
public:
    struct Hooks {
        std::function<void(const std::string& settingsId)>                         edit;
        std::function<void(const std::string& settingsId, const std::string& parameter)> preview;
        // The machine's default setting of the same kind (Reset copies its pipeline).
        std::function<const JPVisionSettings*(JPVisionSettings::Kind kind)>         machineDefault;
        std::function<void()>                                                        changed;
        // A test on the machine: "testAlignment", "detectOffsets", "testFiducial".
        std::function<void(const std::string& settingsId, const std::string& test)>  test;
    };

    // True when `what` ("editPipeline", "resetPipeline", "copyPipeline",
    // "pastePipeline", "parameter:<name>", or a test) is one of these: done, or asked.
    static bool act(JPConfiguration& config, const std::string& settingsId, const std::string& what, const Hooks& hooks);
};

} // inline namespace jf
