// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPChoiceRow.h"
#include "JPFormPage.h"

#include <j/core/JComboBox.h>
#include <j/core/JContainer.h>
#include <j/core/JDataGrid.h>
#include <j/core/JLabel.h>
#include <j/core/JLineEdit.h>
#include <j/core/JTabWidget.h>
#include <j/core/JTreeView.h>
#include <j/core/StackedWidget.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

inline namespace jf {

// Rows (the job's placements, or the library's entries): a list sorted by
// any column (one click on its heading, again to reverse), or a tree grouped
// by any column, switched with one click; a filter that keeps, as you type,
// only the rows with that text in a column shown; several rows chosen at
// once (Shift, Ctrl). Below, tabbed pages of what is chosen (JPFormPage). A
// view: its rows come from the owner, what is chosen goes to the owner.
class JPPartsPanel : public JContainer {
public:
    // One placement: its cells, in the columns' order; its key (designator).
    struct Row {
        std::string              key;
        std::vector<std::string> cells;
    };
    enum class View { List, Tree };

    struct Page {
        std::string                 title;
        std::unique_ptr<JPFormPage> page;
    };
    // `noun`: what a row is, for the count ("placements").
    JPPartsPanel(JSceneGraph& graph, std::vector<std::string> columns, std::string noun, std::vector<Page> pages);
    ~JPPartsPanel() override;

    void showRows(std::vector<Row> rows);
    JPFormPage& page(size_t i) { return *m_pages[i].page; }
    void showPage(size_t i);
    // The rows chosen now, by key.
    std::vector<std::string> chosenKeys() const;
    // As last chosen (kept by the owner): the view, and the column the tree
    // groups by.
    void setView(View view, int groupColumn);

    std::function<void(const std::vector<std::string>& keys)> onChosen;
    std::function<void(const std::string& key)>               onActivated;   // double-clicked
    std::function<void(View view, int groupColumn)>           onViewChanged;

private:
    void refill();
    void fillList();
    void fillTree();
    bool matches(const Row& row) const;
    void chosen(std::vector<std::string> keys);

    std::vector<std::string> m_columns;
    std::vector<Row>         m_rows;
    std::vector<size_t>      m_shown;        // the list's rows, as indices into m_rows
    View                     m_view = View::List;
    int                      m_group = 0;
    std::string              m_filter;
    JLineEdit*               m_filterEdit = nullptr;
    JPChoiceRow*             m_viewChoice = nullptr;
    JLabel*                  m_groupLabel = nullptr;
    JComboBox*               m_groupBy = nullptr;
    // The list and the tree in one place, one shown (the stack does not own them).
    JStackedWidget*          m_stack = nullptr;
    std::unique_ptr<JDataGrid> m_listOwned;
    std::unique_ptr<JTreeView> m_treeOwned;
    JDataGrid*               m_list = nullptr;
    JTreeView*               m_tree = nullptr;
    JLabel*                  m_count = nullptr;
    std::string              m_noun;
    JTabWidget*              m_tabs = nullptr;
    std::vector<Page>        m_pages;
    std::vector<std::string> m_chosen;
    bool                     m_updating = false;
};

} // inline namespace jf
