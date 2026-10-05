// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPIconButton.h"
#include "JPSetupForm.h"
#include "JPTable.h"
#include "JPTextView.h"

#include "pipeline/JPPipeline.h"

#include <j/core/JContainer.h>
#include <j/core/Splitter.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

inline namespace jf {

// The pipeline editor's left side, as OpenPnP's PipelinePanel: its tools
// (Update picture from current view, New stage…, Delete Stage, Copy pipeline
// to clipboard, Create pipeline from clipboard); the stages (Enabled, Name,
// Stage; dragged to reorder) over the chosen stage's description; and the
// chosen stage's settings, those its caller controls shown, not edited.
class JPPipelinePanel : public JContainer {
public:
    JPPipelinePanel(JSceneGraph& graph, JPPipeline& pipeline);
    ~JPPipelinePanel() override;

    // The chosen stage (an index into the pipeline's stages), or -1.
    int  selectedStage() const;
    // The stages again (added, removed, renamed, pasted), the last chosen when `last`.
    void refresh(bool last);
    // The chosen stage's settings again (what its caller controls may have changed).
    void refreshProperties();

    // The pipeline changed (a stage, a setting): to be run again.
    std::function<void()> onChanged;
    // Another stage chosen.
    std::function<void(int index)> onStageSelected;
    // OpenPnP's ClassSelectionDialog: `chosen` gets the class's full name.
    std::function<void(const std::string& title, const std::string& description, std::vector<std::string> classes,
                       std::function<void(std::string)> chosen)>
        chooseClass;
    // Something could not be done: to be said (title, why).
    std::function<void(const std::string& title, const std::string& why)> showError;

private:
    class Model;
    void newStage();
    void deleteStage();
    void copyPipeline();
    void pastePipeline();
    void selectionChanged();
    void refreshDescription();

    JPPipeline&                 m_pipeline;
    std::unique_ptr<Model>      m_model;
    JPTable*                    m_table = nullptr;
    JPTextView*                 m_description = nullptr;
    JPSetupForm*                m_properties = nullptr;
    JSplitter*                  m_split = nullptr;
    JSplitter*                  m_stagesSplit = nullptr;
    std::unique_ptr<JContainer> m_stagesPane, m_tablePane, m_descriptionPane, m_propertiesPane;
};

} // inline namespace jf
