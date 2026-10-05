// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPIconButton.h"
#include "JPSetupForm.h"

#include "machine/JPCellConfig.h"
#include "setup/JPSetupHistory.h"

#include <j/core/JButton.h>
#include <j/core/FrameTimer.h>
#include <j/core/JContainer.h>
#include <j/core/JLabel.h>
#include <j/core/JLineEdit.h>
#include <j/core/MenuSystem.h>
#include <j/core/Splitter.h>
#include <j/core/JScrollArea.h>
#include <j/core/JTreeView.h>

#include <array>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

inline namespace jf {

// Machine Setup: the machine's parts as a tree (JPSetupTree), the selected
// part's settings under it, Add, Remove and Up / Down for the parts, and
// over the tree: open all, close all, and a search filter. Each change, as
// it is committed (Return, Tab or leaving a field, a step of a number, a
// box ticked, a choice made), is handed to the owner, who gives it to the
// running machine and saves it. Undo and Redo step through the changes
// (JPSetupHistory). What is wrong with the setup (a part naming one that is
// not there) is listed, and nothing is handed over until it is put right.
class JPMachineSetupPanel : public JContainer {
public:
    // How much of the width the tree takes beside the settings, to start with.
    static constexpr double kTreeShare = 0.3;
    // How long before a change not taken (the machine was moving) is handed over again, in ms.
    static constexpr float kRetryMs = 500.f;

    // `cell`: the cell as it is. `profiles`: the firmware profiles a
    // controller can name. `selected`: the node to start on (a path, see
    // JPSetupTree), as it was before the panel was made again. `treeShare`:
    // the tree's share of the width beside the settings (the divider between).
    JPMachineSetupPanel(JSceneGraph& graph, JPCellConfig cell, std::vector<JPFirmwareProfile> profiles, std::string selected,
                        double treeShare = kTreeShare);

    // Where the divider between the tree and the settings is now: the tree's share.
    double treeShare() const;
    // The vision settings the Vision nodes choose from, and whose default
    // settings' page they show; what its tests work with.
    void setConfiguration(JPConfiguration* config) { m_config = config; }
    void setVisionTests(JPVisionTests tests) { m_visionTests = std::move(tests); }
    // That page's settings changed (they are the configuration's, not the cell's: no undo here);
    // its buttons and sliders, for the settings shown (as the Vision tab does them).
    std::function<void()> onConfigurationChanged;
    std::function<void(const std::string& settingsId, const std::string& action)> visionAction;
    // The page shown again (the vision settings changed elsewhere).
    void refreshForm();

    // The cell as set up, to be put in use: false when it was not taken
    // (it is handed over again with the next change).
    std::function<bool(const JPCellConfig& cell)> onApply;
    // Undo or Redo became possible or not, or what they would do changed.
    std::function<void()> onHistory;
    // A button on a part's settings (Visual Test, Start Calibration): the
    // part's path and the action's name.
    std::function<void(const std::string& path, const std::string& action)> onAction;
    // Where a camera or nozzle is now (X, Y, Z, rotation; a coordinate not
    // known is empty), and where an axis is.
    using Where = std::array<std::optional<double>, 4>;
    std::function<Where(JPSetupForm::Tool tool)> whereIs;
    std::function<std::optional<double>(const std::string& axisId)> axisAt;
    // OpenPnP's Z probe for a camera's capture: as JPFeedersPanel::probeZ.
    std::function<bool(double x, double y, std::function<void(double z)> done)> probeZ;
    // Take the camera or nozzle to a place (a coordinate left empty stays),
    // or an axis to a position.
    std::function<void(JPSetupForm::Tool tool, const Where& to)> moveTo;
    std::function<void(const std::string& axisId, double to)> moveAxis;
    // OpenPnP's ClassSelectionDialog: one of `classes` chosen (empty: cancelled).
    std::function<void(const std::string& title, const std::string& description, const std::vector<std::string>& classes,
                       std::function<void(std::string)> chosen)> chooseClass;
    // The node selected changed (its path).
    std::function<void(const std::string& path)> onSelected;

    // Show the node at `path` (a camera's, "camera:<id>"): the search cleared
    // so it is in the tree, opened down to it and selected, its settings
    // shown.
    void showNode(const std::string& path);

    // A change made elsewhere (a port chosen on the Machine panel): `edit`
    // changes the setup, it is a step to undo like any other, and it is
    // handed over at once.
    void change(const std::string& what, const std::function<void(JPCellConfig&)>& edit);
    // Something measured (a camera's calibration, the squareness), already
    // in use and saved by the cell: shown here too (not handed back as a
    // change, and kept through undo and redo), the form made again.
    void measured(const std::function<void(JPCellConfig&)>& edit);
    // The shown part's form made again on the next frame (what it shows
    // changed, not only its values).
    void remakeForm();

    bool canUndo() const { return m_history.canUndo(); }
    bool canRedo() const { return m_history.canRedo(); }
    // "Undo Add Camera" / "Redo Add Camera", or plain "Undo" / "Redo".
    std::string undoLabel() const;
    std::string redoLabel() const;
    void undo();
    void redo();
private:
    void rebuildTree();
    // The note line: shown with `text`, gone when it is empty.
    void setNote(const std::string& text);
    // A line under the settings shown with `text`, taking no room without.
    void showLine(JLabel* line, const std::string& text);
    static constexpr float kNoLimit = 100000.f;
    // The tree's rows again, open where m_expanded says.
    void setRows(bool firstTime);
    void addPart();
    void addPart(const std::string& kind);
    void removePart();
    // Open (or close) the selected node and everything under it.
    void setBranch(bool open);
    void collapseAll();
    // Up (-1) or Down (+1), keeping the part selected.
    void moveSelected(int by);
    void select(const std::string& path);
    void show(const std::string& path);
    void changed(const std::string& property);
    // A place row's buttons: take it from the machine (one step to undo), or go there.
    void capture(const JPSetupProperties::Row& row, JPSetupForm::Tool tool);
    // The values captured put in the row (one not known left as it is), a step to undo.
    void applyCapture(const JPSetupProperties::Row& row, const Where& now, const std::string& at);
    void goTo(const JPSetupProperties::Row& row, JPSetupForm::Tool tool);
    // A change was made to m_draft: a step to undo, handed over.
    // `from`: the node selected when it was made, to go back to on Undo.
    void record(const std::string& what, const std::string& key, const std::string& from);
    void restore(const JPSetupHistory::State& state);
    // Hand m_draft over when it differs from what is in use and nothing is wrong with it.
    void handOver();
    void update();
    void collectExpanded(const JTreeViewNode& n);

    JPCellConfig             m_inUse;      // what the machine has (handed over last)
    JPCellConfig             m_draft;      // as set up
    JPCellConfig             m_recorded;   // m_draft as the last step left it
    JPSetupHistory           m_history;
    JFrameTimer              m_retry;
    std::map<std::string, std::string> m_labels;   // the shown form's property names: their labels
    std::vector<JPFirmwareProfile> m_profiles;
    JPConfiguration*               m_config = nullptr;
    JPVisionTests                  m_visionTests;
    // The properties the shown node's page has of the configuration (its vision settings' tab).
    std::set<std::string>          m_configProperties;
    // The form for the node at `path`, the configuration's properties in it noted.
    JPSetupProperties::Form        formFor(const std::string& path);
    // The settings the shown vision node's second tab is for (empty: none).
    std::string                    shownVisionSettings() const;
    std::string              m_selected;
    std::vector<std::string> m_reshaping;   // the shown form's properties that change the form
    std::map<std::string, JPSetupProperties::Form::Edit> m_edits;   // the shown form's buttons that change the part
    std::set<std::string>    m_expanded;    // paths of the tree's open nodes
    std::unique_ptr<JContainer> m_treePane, m_formPane;   // the splitter's panes
    JSplitter*               m_split    = nullptr;
    JTreeView*               m_tree     = nullptr;
    JLineEdit*               m_search   = nullptr;
    std::unique_ptr<JMenu>   m_treeMenu;   // a right-click on the tree
    JMenuItem*               m_menuAdd    = nullptr;
    JMenuItem*               m_menuRemove = nullptr;
    JButton*                 m_add      = nullptr;
    JButton*                 m_remove   = nullptr;
    JButton*                 m_up       = nullptr;
    JButton*                 m_down     = nullptr;
    JLabel*                  m_title    = nullptr;
    JPSetupForm*             m_form     = nullptr;
    std::shared_ptr<bool>    m_alive = std::make_shared<bool>(true);   // for work posted to a later frame
    JLabel*                  m_problems = nullptr;
    JLabel*                  m_note     = nullptr;
};

} // inline namespace jf
