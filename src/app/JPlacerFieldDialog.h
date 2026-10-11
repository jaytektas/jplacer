// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "model/JPConfiguration.h"

#include <j/app/JDialogWindow.h>
#include <j/core/JButton.h>
#include <j/core/JComboBox.h>
#include <j/core/JContainer.h>
#include <j/core/JDialogButtonBox.h>
#include <j/core/JLabel.h>
#include <j/core/JLineEdit.h>

#include <functional>
#include <memory>
#include <string>

inline namespace jf {

// One field of a kind (DESIGN.md, Fields), added or edited (a table's Add Field…, a heading's Edit Field…):
// its Name, its Type and a Choice's choices (a field added), and the headings an import matches it by (a
// field an import fills). A field added, of another type than its values were, has them read again as that
// type (those it cannot read named, and emptied once that is agreed). Delete…: a field added gone with its
// values (how many said first); a built-in removed from every table, the import and the search until
// restored (Edit Fields…); one jplacer needs has no Delete. `done` has the field's key once it is kept
// (removed: true when it went).
class JPlacerFieldDialog : public JDialogWindow {
public:
    static constexpr uint32_t kW = 560, kH = 360;

    JPlacerFieldDialog(JPConfiguration& config, JPJob* job, JPFieldSet::Of of, std::string key,
                       std::function<void(const std::string& key, bool removed)> done, JGpuHal& hal, int sx, int sy,
                       NativeWinHandleType parent);

protected:
    void layout(float w, float h) override;

private:
    // The field as typed: false (and why, in the note) when it cannot be kept.
    bool typed(JPField& f, std::string& why) const;
    void check();
    void keep();
    void remove();
    void finish(const std::string& key, bool removed);

    JPConfiguration&                                         m_config;
    JPJob*                                                   m_job;
    JPFieldSet::Of                                           m_of;
    std::string                                              m_key;   // empty: a new one
    std::function<void(const std::string&, bool)>            m_done;
    std::unique_ptr<JContainer>                              m_content;
    JLabel*                                                  m_about = nullptr;
    JLabel*                                                  m_note = nullptr;
    JLineEdit*                                               m_name = nullptr;
    JComboBox*                                               m_type = nullptr;
    JLineEdit*                                               m_choices = nullptr;
    JLineEdit*                                               m_matches = nullptr;
    std::unique_ptr<JDialogButtonBox>                        m_buttons;
    JButton*                                                 m_ok = nullptr;
};

} // inline namespace jf
