// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "model/JPConfiguration.h"
#include "ui/JPTable.h"

#include <j/app/JDialogWindow.h>
#include <j/core/JButton.h>
#include <j/core/JContainer.h>
#include <j/core/JDialogButtonBox.h>
#include <j/core/JLabel.h>

#include <functional>
#include <memory>
#include <string>

inline namespace jf {

// Every field of a kind (a table's Edit Fields…, DESIGN.md, Fields): its name, type, whose it is (jplacer's or
// yours), whether jplacer needs it, the headings an import matches it by, and how many have a value in it
// (a field of yours); a built-in removed shown as such. Add Field…, Edit… (and a double-click: rename,
// type, matches, delete) and Restore (a built-in removed, back) through `openField` (empty key: a new one)
// and `restored`.
class JPlacerFieldsDialog : public JDialogWindow {
public:
    static constexpr uint32_t kW = 820, kH = 520;

    JPlacerFieldsDialog(JPConfiguration& config, JPJob* job, JPFieldSet::Of of,
                        std::function<void(const std::string& key)> openField, std::function<void()> restored, JGpuHal& hal,
                        int sx, int sy, NativeWinHandleType parent);
    ~JPlacerFieldsDialog() override;

protected:
    void layout(float w, float h) override;

private:
    class Rows;
    // The field of the row chosen, or null.
    const JPField* chosen() const;
    void           update();

    JPConfiguration&                       m_config;
    JPFieldSet::Of                         m_of;
    std::function<void(const std::string&)> m_openField;
    std::function<void()>                  m_restored;
    unsigned                               m_version = 0;
    std::unique_ptr<Rows>                  m_rows;
    std::unique_ptr<JContainer>            m_content;
    JLabel*                                m_note = nullptr;
    JPTable*                               m_table = nullptr;
    JButton*                               m_edit = nullptr;
    JButton*                               m_restore = nullptr;
    std::unique_ptr<JDialogButtonBox>      m_buttons;
};

} // inline namespace jf
