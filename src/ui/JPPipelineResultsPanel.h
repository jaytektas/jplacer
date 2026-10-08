// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPIconButton.h"
#include "JPMatView.h"
#include "JPTextView.h"

#include "pipeline/JPPipeline.h"

#include <j/core/JContainer.h>
#include <j/core/JLabel.h>
#include <j/core/Splitter.h>

#include <memory>

inline namespace jf {

// The pipeline editor's right side, as OpenPnP's ResultsPanel: the stage
// shown, how long it and the whole pipeline took; First, Previous, Next and
// Last stage; Pin (keep showing that stage's result while choosing others);
// true colours or as BGR; its picture, with the colour and place under the
// mouse (or what it found there); and what it found, as text.
class JPPipelineResultsPanel : public JContainer {
public:
    JPPipelineResultsPanel(JSceneGraph& graph, JGpuHal* hal, JPPipeline& pipeline);
    ~JPPipelineResultsPanel() override;

    // After the pipeline ran: the stage shown kept when it is still there; `stopped`, why it stopped (empty: it
    // ran to its end), said above what the stage shown found.
    void refresh(const std::string& stopped);
    // The stage chosen in the stages (an index), or -1.
    void setSelectedStage(int index);

private:
    void update();
    int  shown() const { return m_pinned >= 0 ? m_pinned : m_selected; }
    void showStage(int index);
    void hover(int x, int y);

    JPPipeline&                 m_pipeline;
    int                         m_selected = -1, m_pinned = -1;
    bool                        m_trueColors = true;
    std::string                 m_stopped;   // refresh
    JLabel*                     m_name = nullptr;
    JPIconButton*               m_first = nullptr;
    JPIconButton*               m_previous = nullptr;
    JPIconButton*               m_next = nullptr;
    JPIconButton*               m_last = nullptr;
    JPIconButton*               m_pin = nullptr;
    JPIconButton*               m_color = nullptr;
    JPMatView*                  m_view = nullptr;
    JLabel*                     m_status = nullptr;
    JPTextView*                 m_model = nullptr;
    JSplitter*                  m_split = nullptr;
    std::unique_ptr<JContainer> m_viewPane, m_modelPane;
};

} // inline namespace jf
