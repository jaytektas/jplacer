// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "pipeline/JPPipeline.h"
#include "ui/JPPipelineEditor.h"

#include <j/app/JDialogWindow.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

inline namespace jf {

// OpenPnP's CvPipelineEditorDialog: the pipeline editor in a window of its
// own, nine tenths of the main window's size, on the pipeline it is given
// (changed in place). Closed, `closed` is told whether it was changed from
// `original` (OpenPnP then asks to save).
class JPlacerPipelineEditorDialog : public JDialogWindow {
public:
    static constexpr uint32_t kW = 1200, kH = 800;
    // The main window's place and size, for this one's.
    struct Owner {
        int x = 0, y = 0, width = 0, height = 0;
    };
    using ChooseClass = std::function<void(const std::string& title, const std::string& description, std::vector<std::string> classes,
                                           std::function<void(std::string)> chosen)>;

    JPlacerPipelineEditorDialog(const std::string& title, std::shared_ptr<JPPipeline> pipeline, const std::string& original,
                                Owner owner, ChooseClass chooseClass, std::function<void(bool dirty)> closed, JGpuHal& hal,
                                int sx, int sy, NativeWinHandleType parent);
    ~JPlacerPipelineEditorDialog() override;

protected:
    void layout(float w, float h) override;

private:
    std::shared_ptr<JPPipeline>       m_pipeline;
    std::unique_ptr<JPPipelineEditor> m_editor;
    std::function<void(bool)>         m_closed;
};

} // inline namespace jf
