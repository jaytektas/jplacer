// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPPropertyForm.h"

#include "machine/JPCellConfig.h"

#include <j/core/JButton.h>
#include <j/core/JContainer.h>
#include <j/core/JLabel.h>
#include <j/core/JLineEdit.h>
#include <j/core/JScrollArea.h>
#include <j/core/JTreeView.h>

#include <functional>
#include <set>
#include <string>
#include <vector>

inline namespace jf {

// Machine Setup: the machine's parts as a tree (JPSetupTree), the selected
// part's settings under it, Add, Remove and Up / Down for the parts, and a
// search over the tree. Edits are made to a copy of the cell; Apply hands
// the copy to the owner (who saves it and opens the cell again), Reset goes
// back to the cell as it is. What is wrong with the copy (a part naming one
// that is not there) is listed, and Apply waits until it is put right.
class JPMachineSetupPanel : public JContainer {
public:
    // `cell`: the cell as it is. `profiles`: the firmware profiles a
    // controller can name. `selected`: the node to start on (a path, see
    // JPSetupTree), as it was before the cell was opened again.
    JPMachineSetupPanel(JSceneGraph& graph, JPCellConfig cell, std::vector<std::string> profiles, std::string selected);

    // Apply pressed: the cell as set up.
    std::function<void(const JPCellConfig& cell)> onApply;
    // The node selected changed (its path).
    std::function<void(const std::string& path)> onSelected;

private:
    void rebuildTree();
    // Up (-1) or Down (+1), keeping the part selected.
    void moveSelected(int by);
    void select(const std::string& path);
    void show(const std::string& path);
    void changed(const std::string& property);
    void update();
    void collectExpanded(const JTreeViewNode& n);

    JPCellConfig             m_original;
    JPCellConfig             m_draft;
    std::vector<std::string> m_profiles;
    std::string              m_selected;
    std::vector<std::string> m_reshaping;   // the shown form's properties that change the form
    std::set<std::string>    m_expanded;    // paths of the tree's open nodes
    JTreeView*               m_tree     = nullptr;
    JButton*                 m_add      = nullptr;
    JButton*                 m_remove   = nullptr;
    JButton*                 m_up       = nullptr;
    JButton*                 m_down     = nullptr;
    JLabel*                  m_title    = nullptr;
    JScrollArea*             m_scroll   = nullptr;
    JPPropertyForm*          m_form     = nullptr;
    JLabel*                  m_problems = nullptr;
    JLabel*                  m_note     = nullptr;
    JButton*                 m_reset    = nullptr;
    JButton*                 m_apply    = nullptr;
};

} // inline namespace jf
