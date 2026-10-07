// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPCellWatch.h"

#include "common/JPLogLevels.h"
#include "machine/JPCell.h"

#include <j/core/JButton.h>
#include <j/core/JCheckBox.h>
#include <j/core/JComboBox.h>
#include <j/core/JContainer.h>
#include <j/core/JLabel.h>
#include "JPHistoryLineEdit.h"

#include <j/core/JLineEdit.h>
#include <j/core/JTextArea.h>
#include <j/core/MenuSystem.h>

#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

inline namespace jf {

// What passes between jplacer and the controllers (status reports left out)
// and what jplacer's log says, newest last, and a line to send to a
// controller as typed.
//
// Over the lines, what is shown: **G-code** (the controllers' traffic, on or
// off), **Log** (how much the log says, every category), and **Categories ▾**
// (a menu: each category as the Log level, or a level of its own). The log's
// levels are the log's own (JLog): they set what is written to the log file
// too. The console follows its newest line while scrolled to the end. Its
// lines are one read-only text: dragged over, Ctrl+A, and Copy (Ctrl+C or
// the right-click menu: Copy, Select All, Clear) take them to paste.
class JPConsolePanel : public JContainer {
public:
    JPConsolePanel(JSceneGraph& graph, JPCell& cell, bool showTraffic);
    ~JPConsolePanel() override;

    // The traffic shown or not, the log's levels changed: for the owner to keep.
    std::function<void(bool)>                onShowTraffic;
    std::function<void(const JPLogLevels&)>  onLogLevels;
    // Opens a menu at (x, y) in the window (the owner's popup).
    std::function<void(JMenu* menu, float x, float y)> openMenu;

private:
    // A line kept: the controllers' traffic, or the log's (its level and category), and its words.
    struct Line {
        bool        traffic = false;
        JLogLevel   level = JLogLevel::Info;
        std::string category;
        std::string text;
    };
    // Lines added at the end, the oldest let go past the history kept; each shown as the choices over
    // the lines say (G-code, Log, Categories).
    void addLines(std::vector<Line> lines);
    bool shows(const Line& line) const;
    // What is shown made again from the lines kept, as the choices now say.
    void refilter();
    void clear();
    // Lines come in on any thread; they are taken in on the main one, many at once.
    void takeLogLines();
    void showCategories();
    void levelsChanged();
    void send();

    JPCell&                  m_cell;
    JTextArea*               m_text       = nullptr;
    JCheckBox*               m_traffic    = nullptr;
    JComboBox*               m_level      = nullptr;
    JButton*                 m_categories = nullptr;
    JComboBox*               m_controller = nullptr;
    JPHistoryLineEdit*       m_input      = nullptr;
    JCheckBox*               m_upperCase  = nullptr;   // OpenPnP's Force Upper Case
    std::unique_ptr<JMenu>   m_menu;
    std::unique_ptr<JMenu>   m_textMenu;   // the lines' right-click menu
    std::vector<std::unique_ptr<JMenu>> m_submenus;
    std::deque<Line>         m_lines;       // the history kept, oldest first
    std::deque<size_t>       m_shownSizes;  // each line's length in the text, its newline counted (0: not shown)
    JPLogLevels              m_levels = JPLogLevels::current();   // the choices, as last set
    bool                     m_showTraffic = true;
    int                      m_listener = 0;
    // Log lines waiting for the main thread: shared with the log's listener,
    // which may still be running on another thread as the panel goes.
    struct Inbox {
        std::mutex               mutex;
        std::vector<Line>        lines;
    };
    std::shared_ptr<Inbox>   m_inbox = std::make_shared<Inbox>();
    std::shared_ptr<bool>    m_alive = std::make_shared<bool>(true);
    JPCellWatch              m_watch;
};

} // inline namespace jf
