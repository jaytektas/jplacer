// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPGroupFrame.h"
#include "JPIconButton.h"
#include "JPPlacementsHolderTableModel.h"
#include "JPTable.h"

#include "model/JPConfiguration.h"
#include "model/JPJob.h"

#include <j/core/MenuSystem.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

inline namespace jf {

// The Boards tab's Boards group, or the Panels tab's Panels group, as
// OpenPnP's BoardsPanel and PanelsPanel have them: Add (Create New…,
// Existing), Remove, Copy…, Clean Up, and the table of those known (name,
// width, length).
class JPPlacementsHoldersGroup : public JPGroupFrame {
public:
    JPPlacementsHoldersGroup(JSceneGraph& graph, JPConfiguration& config, JPPlacementsHolder::Kind kind,
                             std::function<const JPJob*()> job);

    // One added, changed or taken away (to be saved, other views told).
    std::function<void()> onChanged;
    // The one shown below changed: the only one chosen, or none.
    std::function<void(JPPlacementsHolder*)> onShown;
    std::function<void(JMenu*, float x, float y)> openMenu;
    // Asks whether to save one with changes before it is taken away (Yes,
    // No, Cancel); `then` runs after any answer.
    std::function<void(JPPlacementsHolder&, std::function<void()> then)> confirmSave;

    void refresh();
    void select(const JPPlacementsHolder* h);
    JPPlacementsHolder* shown() const { return m_shown; }

private:
    struct Words;
    std::vector<std::string> saveExtensions() const;
    std::vector<std::string> openExtensions() const;
    const Words& words() const;
    std::vector<JPPlacementsHolder*> selections() const;
    void selectionChanged();
    void showAddMenu();
    void addFile(const std::string& path, const char* errorTitle);
    void remove(std::vector<std::string> files, bool reportInUse, bool repeat, bool removedAny);
    void copy();
    void changed();
    const std::vector<std::shared_ptr<JPPlacementsHolder>> known() const;

    JPConfiguration&              m_config;
    JPPlacementsHolder::Kind      m_kind;
    std::function<const JPJob*()> m_job;
    JPPlacementsHolderTableModel  m_model;
    JPTable*                      m_table = nullptr;
    JPIconButton*                 m_add = nullptr;
    JPIconButton*                 m_remove = nullptr;
    JPIconButton*                 m_copy = nullptr;
    std::unique_ptr<JMenu>        m_addMenu;
    JPPlacementsHolder*           m_shown = nullptr;
};

} // inline namespace jf
