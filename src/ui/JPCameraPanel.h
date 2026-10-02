// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPCameraView.h"
#include "JPChoiceRow.h"

#include "camera/JPCameraFeed.h"
#include "machine/JPCellConfig.h"

#include <j/core/JContainer.h>
#include <j/core/JLabel.h>

#include <functional>
#include <memory>
#include <vector>

inline namespace jf {

// The cell's cameras: choose one, see it live. Only the camera shown runs —
// the others are stopped — so a camera nobody is looking at costs nothing.
class JPCameraPanel : public JContainer {
public:
    JPCameraPanel(JSceneGraph& graph, JGpuHal& hal, const JPCellConfig& cell);
    ~JPCameraPanel() override;

    // The camera shown changed (its id), and the one before it (empty at
    // first): the owner switches the cameras' lights.
    std::function<void(const std::string& shown, const std::string& before)> onShown;

    void show(size_t index);
    // The camera shown (its id), empty when there is none.
    std::string shownId() const;
    // A word about the picture beside the mode (e.g. why it is dark).
    void setNote(const std::string& text);

private:
    std::vector<std::unique_ptr<JPCameraFeed>> m_feeds;
    JPCameraView*                              m_view  = nullptr;
    JLabel*                                    m_state = nullptr;
    JLabel*                                    m_note  = nullptr;
    size_t                                     m_shown = SIZE_MAX;
    std::vector<std::function<void()>>         m_unwatch;
    std::shared_ptr<bool>                      m_alive = std::make_shared<bool>(true);
};

} // inline namespace jf
