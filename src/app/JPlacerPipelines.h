// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPlacerMachine.h"

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

    // What the pipeline runs with: the head camera, and the configuration in `directory`.
    void useHeadCamera(JPPipeline& pipeline, const std::string& directory) const;
    // The editor on `pipeline`, titled `title`; changes kept go to `keep`.
    // `original`: how it was before edits not yet kept (empty: as it is).
    void edit(const std::string& title, std::shared_ptr<JPPipeline> pipeline, std::function<void(const JPPipeline&)> keep,
              const std::string& original = {});

private:
    JAppWindow&     m_window;
    JPlacerMachine& m_machine;
};

} // inline namespace jf
