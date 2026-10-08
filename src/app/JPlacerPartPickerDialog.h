// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "model/JPConfiguration.h"
#include "model/JPPartChoice.h"
#include "model/JPPartMatcher.h"

#include <j/app/JDialogWindow.h>
#include <j/core/JButton.h>
#include <j/core/JCheckBox.h>
#include <j/core/JContainer.h>
#include <j/core/JDataGrid.h>
#include <j/core/JDialogButtonBox.h>
#include <j/core/JLabel.h>
#include <j/core/JLineEdit.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

inline namespace jf {

// The part picker (DESIGN.md, Matching) for a placement's board part: what
// the files said about it (value, footprint, MPN, manufacturer, supplier…)
// and what it is now; the library's parts it may be, best first, each saying
// why (JPPartMatcher); a filter (its [x] clears it) to look through the whole
// library; and what to make it:
//  * Use This Part (Return, or a double-click on one): the library's part;
//    With Remember ticked, the library keeps what the files call it (its
//    value and footprint, MPN, supplier's part number: JPLibraryLearning),
//    so the next board that calls it so is matched without asking;
//  * Add to Library: a library part made from all the files said;
//  * Make It the Board's Own: a part (and package) of the board's, from
//    what the files said, kept in the board;
//  * Leave to Be Chosen.
// For every placement of the board part, or, Only <designator> ticked, for
// the one placement alone. Escape, Cancel and the [x] change nothing.
class JPlacerPartPickerDialog : public JDialogWindow {
public:
    static constexpr uint32_t kW = 940, kH = 620;

    JPlacerPartPickerDialog(const JPConfiguration& config, const JPBoard& board, const std::string& placementId,
                            std::function<void(const JPPartChoice&)> chosen, JGpuHal& hal, int sx, int sy,
                            NativeWinHandleType parent);

protected:
    void layout(float w, float h) override;
    void onMouse(float mx, float my, bool pressed, bool released, bool held) override;

private:
    void fill();
    void choose(JPPartChoice::Kind kind);

    const JPConfiguration&                  m_config;
    std::function<void(const JPPartChoice&)> m_chosen;
    JPBoardPart                             m_part;       // what it is now (a copy: the board may change)
    std::string                             m_designator;
    std::vector<JPPartMatcher::Candidate>   m_candidates;
    std::vector<const JPPart*>              m_shown;      // the list's rows' parts
    int                                     m_activated = -1;
    bool                                    m_focusPlaced = false;

    std::unique_ptr<JContainer>       m_content;
    std::vector<JLabel*>              m_notes;
    JLineEdit*                        m_filter = nullptr;
    JDataGrid*                        m_list = nullptr;
    JCheckBox*                        m_onlyThis = nullptr;
    JCheckBox*                        m_remember = nullptr;
    std::unique_ptr<JDialogButtonBox> m_buttons;
    JButton*                          m_use = nullptr;
};

} // inline namespace jf
