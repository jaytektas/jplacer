// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPKeyMap.h"

#include <j/app/JDialogWindow.h>
#include <j/core/JCheckBox.h>
#include <j/core/JComboBox.h>
#include <j/core/JContainer.h>
#include <j/core/JDialogButtonBox.h>
#include <j/core/JKeySequenceEdit.h>
#include <j/core/JLabel.h>
#include <j/core/JScrollArea.h>
#include <j/core/JTabWidget.h>

#include <functional>
#include <map>
#include <memory>
#include <string>

inline namespace jf {

class JPTextField;

// Edit > Preferences, on three tabs:
//
//  - General: the look (theme, interface scale), tear-off menus, the
//    applications-menu entry, the parts library's folder, and updates.
//  - Keys: every function a key can be given (JPKeyMap), by group: click its
//    box and press the key (Escape leaves it as it was); Clear takes it off;
//    Reset All puts back every default.
//  - Jog: the Jog panel's distance and speed steps, which also appear on the
//    Keys tab to be given a key (1 for 1 mm, say).
//
// Live-apply: each control writes its setting, applies it and saves the file
// the moment it changes (a text field as it commits: Return, Tab or leaving
// it), so there is nothing to lose by closing the window and no Apply button
// to forget.
//
// Opened through JAppWindow::openModal, which supplies the trailing hal /
// position / parent arguments.
class JPlacerPreferencesDialog : public JDialogWindow {
public:
    // The opening size; the window can be resized from there.
    static constexpr uint32_t kW = 560, kH = 560;

    // `onCheckNow` runs after the dialog has closed: the answer can take seconds
    // to arrive and is reported in the main window, not in this one.
    // `onScale`: an interface scale chosen (JPlacerAppearance::scales), for
    // the main window to apply. `keys`: what the Keys tab shows and changes.
    // `onJogSteps`: the jog steps changed (kept in the settings already).
    // `onLibraryFolder`: another folder for the parts library (empty: the
    // default), to open the library from.
    JPlacerPreferencesDialog(std::function<void()> onCheckNow, std::function<void(double)> onScale, JPKeyMap& keys,
                             std::function<void()> onJogSteps, std::function<void(std::string)> onLibraryFolder,
                             JGpuHal& hal, int sx, int sy, NativeWinHandleType parent);

protected:
    void layout(float w, float h) override;

private:
    static float pad();
    // A note under a section's rows, wrapping to the page's width (layout
    // makes it as tall as its lines).
    std::unique_ptr<JLabel> note(const std::string& text);
    std::unique_ptr<JContainer> generalPage(std::function<void(double)> onScale, std::function<void(std::string)> onLibraryFolder);
    std::unique_ptr<JContainer> keysPage();
    std::unique_ptr<JContainer> jogPage();
    // The Keys tab's rows made again (the functions changed: new jog steps).
    void fillKeys();
    // Every key box shows the key its function has now.
    void showKeys();

    std::function<void()>             m_onCheckNow;
    JPKeyMap&                         m_keys;
    std::function<void()>             m_onJogSteps;
    std::unique_ptr<JTabWidget>       m_tabs;
    std::unique_ptr<JContainer>       m_general, m_keysTab, m_jog;
    std::unique_ptr<JDialogButtonBox> m_buttons;
    JScrollArea*                      m_keyList = nullptr;
    JLabel*                           m_keyNote = nullptr;   // what the last assignment did
    std::map<std::string, JKeySequenceEdit*> m_keyBoxes;     // by function id
    JLabel*                           m_jogNote = nullptr;   // why steps typed were not taken
    std::vector<JLabel*>              m_notes;
};

} // inline namespace jf
