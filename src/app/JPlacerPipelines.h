// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPlacerMachine.h"

#include "model/JPConfiguration.h"
#include "pipeline/JPPipeline.h"

#include <j/app/JAppWindow.h>

#include <functional>
#include <memory>
#include <string>

inline namespace jf {

// OpenPnP's pipelines from the app: one given the head camera (its picture,
// its scale, its places) and the configuration's directory; the pipeline
// editor opened on one, and on closing it changed, Save pipeline changes?
// (Yes keeps them, No drops them, Cancel goes back to editing).
class JPlacerPipelines {
public:
    JPlacerPipelines(JAppWindow& window, JPlacerMachine& machine);

    // What the pipeline runs with: the head camera (or `feed`), and the configuration in `directory`.
    void useHeadCamera(JPPipeline& pipeline, const std::string& directory) const;
    void useCamera(JPPipeline& pipeline, JPCameraFeed* feed, const std::string& directory) const;
    // A vision setting's pipeline in the editor, prepared as OpenPnP
    // prepares it for the part or package given (bottom vision: else the one
    // chosen on the Parts or Packages tab); kept, `kept` is told.
    void editVision(JPConfiguration& config, const std::string& settingsId, const std::string& partId,
                    const std::string& packageId, std::function<void()> kept);
    // OpenPnP's previewParameterChangeEffect: the setting's pipeline run with
    // its values, the parameter's effect stage's picture then the result
    // shown on the camera, "Label = value" over them.
    void previewVision(JPConfiguration& config, const std::string& settingsId, const std::string& partId,
                       const std::string& packageId, const std::string& parameter);
    // The editor on `pipeline`, titled `title`; changes kept go to `keep`.
    // `original`: how it was before edits not yet kept (empty: as it is).
    void edit(const std::string& title, std::shared_ptr<JPPipeline> pipeline, std::function<void(const JPPipeline&)> keep,
              const std::string& original = {});

    // The part chosen on the Parts tab, the package on the Packages tab
    // (bottom vision's stand-ins for a setting shown on the Vision tab); "" for none.
    std::function<std::string()> chosenPart, chosenPackage;

private:
    // The setting's pipeline on its camera, prepared; null (and why) when it cannot be.
    std::shared_ptr<JPPipeline> prepared(JPConfiguration& config, const JPVisionSettings& settings, const std::string& partId,
                                         const std::string& packageId, JPCameraFeed*& feed, std::string& why) const;

    JAppWindow&     m_window;
    JPlacerMachine& m_machine;
    int             m_previews = 0;   // the newest preview, so an older one's later picture is not shown
};

} // inline namespace jf
