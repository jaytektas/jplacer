// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "model/JPConfiguration.h"

#include <j/app/JDialogWindow.h>
#include <j/core/JButton.h>
#include <j/core/JContainer.h>
#include <j/core/JDialogButtonBox.h>
#include <j/core/JLabel.h>
#include <j/core/JLineEdit.h>
#include <j/core/JScrollArea.h>

#include <functional>
#include <memory>

inline namespace jf {

// The manufacturers the library knows and the other names files give them
// (Parts tab, a part's Library page, Manufacturers' Names…): a row each, its
// name and its other names (commas between), the cross deleting it; Add a
// Manufacturer. An MPN's manufacturer in a BOM is the library's by any of its
// names (JPConfiguration::sameManufacturer). Kept as typed; `changed` when
// closed, for the library to be saved.
class JPlacerManufacturersDialog : public JDialogWindow {
public:
    static constexpr uint32_t kW = 720, kH = 480;

    JPlacerManufacturersDialog(JPConfiguration& config, std::function<void()> changed, JGpuHal& hal, int sx, int sy,
                               NativeWinHandleType parent);
    ~JPlacerManufacturersDialog() override;

protected:
    void layout(float w, float h) override;

private:
    void fill();

    JPConfiguration&                  m_config;
    std::function<void()>             m_changed;
    bool                              m_refill = true;
    std::unique_ptr<JContainer>       m_content;
    JLabel*                           m_note = nullptr;
    JScrollArea*                      m_rows = nullptr;
    std::unique_ptr<JDialogButtonBox> m_buttons;
};

} // inline namespace jf
