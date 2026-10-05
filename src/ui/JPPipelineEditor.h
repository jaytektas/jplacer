// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPPipelinePanel.h"
#include "JPPipelineResultsPanel.h"

#include "pipeline/JPPipeline.h"

#include <j/core/JContainer.h>
#include <j/core/Splitter.h>

#include <functional>
#include <memory>
#include <string>

inline namespace jf {

// OpenPnP's CvPipelineEditor: the stages and their settings beside each
// stage's result, the pipeline run again on every change. Whether it was
// changed is told against how it was when the editor opened.
class JPPipelineEditor : public JContainer {
public:
    // `original`: the pipeline as it was before (an editor reopened on
    // edits not yet kept); empty: as it is now.
    JPPipelineEditor(JSceneGraph& graph, JGpuHal* hal, JPPipeline& pipeline, const std::string& original = {});
    ~JPPipelineEditor() override;

    JPPipeline& pipeline() { return m_pipeline; }
    // Run it, and show the results; a failure that stopped it said.
    void process();
    bool isDirty() const;
    // Back to how it was when the editor opened.
    void undoEdits();

    // A pipeline that could not run, or an edit that could not be made: to be said (title, why).
    std::function<void(const std::string& title, const std::string& why)> showError;
    // OpenPnP's ClassSelectionDialog, for a new stage.
    std::function<void(const std::string& title, const std::string& description, std::vector<std::string> classes,
                       std::function<void(std::string)> chosen)>
        chooseClass;

private:
    JPPipeline&                             m_pipeline;
    std::string                             m_original;
    std::unique_ptr<JPPipelinePanel>        m_stages;
    std::unique_ptr<JPPipelineResultsPanel> m_results;
    JSplitter*                              m_split = nullptr;
};

} // inline namespace jf
