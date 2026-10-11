// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "model/JPConfiguration.h"
#include "ui/JPTable.h"

#include <j/app/JDialogWindow.h>
#include <j/core/JContainer.h>
#include <j/core/JDialogButtonBox.h>
#include <j/core/JLabel.h>

#include <functional>
#include <memory>

inline namespace jf {

// The job's values of the fields added to jobs (Job ▸ Job Fields…, DESIGN.md, Fields): a row a field, its
// value edited in place as its type reads it (a length in the System Units, Yes/No as yes or no, a choice by
// name); Add Field… and Edit Fields… as every table's. A value changed: `changed` (the job to be saved).
class JPlacerJobFieldsDialog : public JDialogWindow {
public:
    static constexpr uint32_t kW = 640, kH = 420;

    JPlacerJobFieldsDialog(JPConfiguration& config, JPJob& job, std::function<void()> changed, JGpuHal& hal, int sx, int sy,
                           NativeWinHandleType parent);
    ~JPlacerJobFieldsDialog() override;

protected:
    void layout(float w, float h) override;

private:
    class Rows;

    JPConfiguration&                  m_config;
    unsigned                          m_version = 0;
    std::unique_ptr<Rows>             m_rows;
    std::unique_ptr<JContainer>       m_content;
    JLabel*                           m_note = nullptr;
    JPTable*                          m_table = nullptr;
    std::unique_ptr<JDialogButtonBox> m_buttons;
};

} // inline namespace jf
