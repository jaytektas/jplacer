// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "ui/JPReticle.h"

#include <j/app/JDialogWindow.h>
#include <j/core/JColorButton.h>
#include <j/core/JContainer.h>
#include <j/core/JDialogButtonBox.h>
#include <j/core/JLabel.h>
#include <j/core/JSpinBox.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

inline namespace jf {

// A camera's Reticle Style (its picture's right-click menu): the reticle's
// colour and line width (the cross and what is drawn to scale), and a
// footprint's drawn over the picture. A colour from the colour chooser, its
// ✕ the theme's again; a width in the theme's lines, 1 to
// JPReticle::kMostWidth. Each change is shown at once (`changed`), and kept
// with the camera's reticle; Close (or Escape) ends it.
class JPlacerReticleStyleDialog : public JDialogWindow {
public:
    static constexpr uint32_t kW = 460, kH = 180;

    JPlacerReticleStyleDialog(const std::string& camera, const JPReticle& reticle,
                              std::function<void(const JPReticle&)> changed, JGpuHal& hal, int sx, int sy,
                              NativeWinHandleType parent);

protected:
    void layout(float w, float h) override;

private:
    void changedNow();

    JPReticle                                m_reticle;
    std::function<void(const JPReticle&)>    m_changed;
    std::unique_ptr<JContainer>              m_content;
    JColorButton*                            m_colour = nullptr;
    JColorButton*                            m_footprintColour = nullptr;
    JSpinBox*                                m_line = nullptr;
    JSpinBox*                                m_footprintLine = nullptr;
    std::unique_ptr<JDialogButtonBox>        m_buttons;
};

} // inline namespace jf
