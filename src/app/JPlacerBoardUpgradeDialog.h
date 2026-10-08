// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "model/JPBoard.h"
#include "model/JPBoardUpgrade.h"

#include <j/app/JDialogWindow.h>
#include <j/core/JButton.h>
#include <j/core/JComboBox.h>
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

// A board upgraded to a new revision from its new files (DESIGN.md, Board revisions; JPBoardUpgrade): one
// summary to work from (the origin's move; how many unchanged, moved, part or footprint changed, new,
// renamed, removed), a Show choice opening each count's list, every placement as it was and is (what
// changed, in words), and the labels: the new revision's, and, for a board that kept none yet, the one it
// is now. Make the New Revision keeps the one shown and shows the new one; Escape, Cancel and the [x]
// change nothing.
class JPlacerBoardUpgradeDialog : public JDialogWindow {
public:
    static constexpr uint32_t kW = 860, kH = 600;

    // `made` has the new revision and the label of the one shown now (for a board that kept none).
    JPlacerBoardUpgradeDialog(JPBoard& board, std::shared_ptr<JPBoard> files,
                              std::function<void(JPBoardRevision, std::string)> made, JGpuHal& hal, int sx, int sy,
                              NativeWinHandleType parent);

protected:
    void layout(float w, float h) override;

private:
    void fill();
    void make();

    const JPBoard&                                     m_board;
    std::shared_ptr<JPBoard>                           m_files;
    JPBoardUpgrade                                     m_upgrade;
    bool                                               m_firstRevision;
    std::function<void(JPBoardRevision, std::string)>  m_made;

    std::unique_ptr<JContainer>       m_content;
    std::vector<JLabel*>              m_notes;
    JLineEdit*                        m_current = nullptr;
    JLineEdit*                        m_label = nullptr;
    JComboBox*                        m_show = nullptr;
    JDataGrid*                        m_list = nullptr;
    std::unique_ptr<JDialogButtonBox> m_buttons;
    JButton*                          m_make = nullptr;
};

} // inline namespace jf
