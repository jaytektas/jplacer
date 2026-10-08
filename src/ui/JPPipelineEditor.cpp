// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPPipelineEditor.h"

#include "openpnp/JPXmlReader.h"
#include "openpnp/JPXmlWriter.h"

inline namespace jf {

namespace {

// The stages a quarter of the width, the results the rest.
constexpr float kStagesShare = 0.25f;

} // namespace

JPPipelineEditor::JPPipelineEditor(JSceneGraph& graph, JGpuHal* hal, JPPipeline& pipeline, const std::string& original)
    : JContainer(graph, 0.f, 0.f), m_pipeline(pipeline),
      m_original(original.empty() ? pipeline.storedText() : original) {
    setDirection(JFlexDirection::Column)->setAlignItems(JAlignItems::Stretch);
    m_results = std::make_unique<JPPipelineResultsPanel>(graph, hal, pipeline);
    m_stages = std::make_unique<JPPipelinePanel>(graph, pipeline);
    m_stages->onChanged = [this] { process(); };
    m_stages->onStageSelected = [this](int index) { m_results->setSelectedStage(index); };
    m_stages->chooseClass = [this](const std::string& title, const std::string& description, std::vector<std::string> classes,
                                   std::function<void(std::string)> chosen) {
        if (chooseClass) chooseClass(title, description, std::move(classes), std::move(chosen));
    };
    m_stages->showError = [this](const std::string& title, const std::string& why) {
        if (showError) showError(title, why);
    };
    m_split = add(std::make_unique<JSplitter>(graph, JSplitter::JOrientation::Horizontal, 0.f, 0.f));
    m_split->setHostsPanes(true);
    m_split->setVSizePolicy(JSizePolicyMode::Expanding, 1);
    m_split->addPane(m_stages.get(), kStagesShare);
    m_split->addPane(m_results.get(), 1 - kStagesShare);
    process();
    m_results->setSelectedStage(m_stages->selectedStage());
    // Now run, what its caller controls is known.
    m_stages->refreshProperties();
}

JPPipelineEditor::~JPPipelineEditor() = default;

void JPPipelineEditor::process() {
    std::string why;
    if (!m_pipeline.process(why) && showError) showError("Error", why);
    m_results->refresh();
}

// As kept, not as it ran: what its parameter stages wrote into their stages is not an edit.
bool JPPipelineEditor::isDirty() const { return m_pipeline.storedText() != m_original; }

void JPPipelineEditor::undoEdits() {
    JPXmlElement root;
    std::string error;
    if (JPXmlReader::parse(m_original, root, error)) m_pipeline.stages() = JPPipeline::fromXml(root).stages();
}

} // inline namespace jf
