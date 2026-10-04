// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPTableModel.h"

#include <j/core/JControl.h>
#include <j/core/JTextEditCore.h>
#include <j/core/MenuSystem.h>

#include <functional>
#include <memory>
#include <optional>
#include <regex>
#include <set>
#include <string>
#include <utility>
#include <vector>

inline namespace jf {

// A table as OpenPnP's (a Swing JTable with a TableRowSorter), over a
// JPTableModel:
//
//  - SORTING: a click on a column's heading makes it the first sort key,
//    ascending; a click on the first key's heading turns it round. Up to
//    three keys are kept, the older ones fainter in their headings. Numbers
//    compare as numbers, ticks false before true, text as a collator does
//    (letters before case).
//  - SEARCH: setFilter takes a regular expression, matched without regard to
//    case anywhere in any column, as OpenPnP's search boxes do.
//  - SELECTION: click, Shift+click a range, Ctrl+click one more; the arrows,
//    Home, End, Page Up and Down move it (Shift extends); Ctrl+A all.
//  - EDITING: a text cell is edited on a double-click, F2 or by typing (all
//    its text chosen first); Return or Tab keeps it, Escape puts it back. A
//    tick box changes on a click; a choice opens its menu on a click.
//  - Ctrl+C copies the chosen rows, a tab between cells.
//  - A column's heading shows its tooltip; drag a heading's edge to widen it.
//
// Rows are the model's: what the table reports and is told is always a
// model row, whatever the sort.
class JPTable : public JControl {
public:
    explicit JPTable(JSceneGraph& graph);

    void setModel(JPTableModel* model);
    // The model's rows changed: sorted and filtered again, the chosen rows
    // kept (by JPTableModel::rowKey).
    void refresh();

    void setFilter(const std::string& regex);   // empty: every row
    const std::string& filter() const { return m_filterText; }

    // Model rows chosen, in the order shown.
    std::vector<int> selectedRows() const;
    int  selectedRow() const;   // the one row chosen, or -1
    void selectRow(int modelRow);   // and shown
    void selectRows(const std::vector<int>& modelRows);
    void clearSelection();
    int  rowAt(float y) const;      // model row under a window y, or -1

    void  setColumnWidth(int c, float w);
    float columnWidth(int c) const;

    // A model row's index in the view (after sort and filter), or -1.
    int viewIndexOf(int modelRow) const;
    // The model row shown at a view index, or -1.
    int modelRowAt(int viewIndex) const {
        return viewIndex >= 0 && size_t(viewIndex) < m_view.size() ? m_view[size_t(viewIndex)] : -1;
    }
    int viewRowCount() const { return int(m_view.size()); }

    jf::JSignal<>    onSelectionChanged;
    jf::JSignal<int> onRowActivated;   // a double-click on a cell that is not edited
    // An edit refused (the model's reason).
    std::function<void(const std::string&)> onEditRefused;
    // Opens a menu at window coordinates (a choice cell's).
    std::function<void(JMenu*, float x, float y)> openMenu;
    // Before the right-click menu opens: the model row under the pointer (-1: none).
    std::function<void(int row)> onContextMenu;
    // A key pressed while no cell is edited, before the table's own keys:
    // true when taken (OpenPnP's placements tables take Space).
    std::function<bool(const JKeyEvent&)> onKey;

    void populateRenderPrimitives(JPrimitiveBuffer& buf) override;
    void handleMousePress(float mx, float my) override;
    void handleMouseRelease(float mx, float my) override;
    void handleMouseMove(float mx, float my) override;
    bool handleScroll(float mx, float my, float wheel) override;
    bool handleKeyEvent(const JKeyEvent& ke) override;
    void prepareContextMenu(float mx, float my) override;
    void onFocusEvent(bool focused) override;
    void endEdit() override { stopEditing(true); }

private:
    struct SortKey {
        int  column;
        bool ascending;
    };

    void rebuildView();
    int  compareRows(int a, int b, int column) const;
    bool rowPasses(int row) const;

    JRect bounds() const;
    float headerHeight() const;
    float rowHeight() const;
    float totalColumnsWidth() const;
    float columnX(int c) const;   // from the table's left edge, before scrolling
    int   columnAt(float mx) const;
    int   dividerAt(float mx, float my) const;
    int   viewRowAt(float my) const;
    void  materialiseWidths() const;
    void  clampScroll();
    void  ensureVisible(int viewRow);
    JRect cellRect(int viewRow, int c) const;

    void clickHeader(int c);
    void selectView(int viewRow, bool shift, bool ctrl);
    void moveLead(int viewRow, bool extend);
    void selectionChanged();

    void startEditing(int modelRow, int c, const std::string* typed);
    // `keep`: the value taken; `stayIfRefused`: a value refused keeps the
    // cell open (Return, Tab), else it is put back. False while still open.
    bool stopEditing(bool keep, bool stayIfRefused = false);
    void openChoices(int modelRow, int c);
    void copySelection() const;

    JPTableModel*                  m_model = nullptr;
    std::vector<int>               m_view;            // view index -> model row
    std::vector<std::string>       m_keys;            // model row -> its key, as last built
    std::vector<SortKey>           m_sortKeys;        // first is the primary
    std::string                    m_filterText;
    std::optional<std::regex>      m_filter;
    std::set<int>                  m_selected;        // model rows
    int                            m_lead = -1;       // model row
    int                            m_leadColumn = 0;
    int                            m_anchor = -1;     // model row a Shift range runs from
    mutable std::vector<float>     m_widths;
    float                          m_scrollX = 0, m_scrollY = 0;
    int                            m_resizing = -1;
    float                          m_resizeFromX = 0, m_resizeFromW = 0;
    bool                           m_draggingV = false;
    float                          m_dragFromY = 0, m_dragFromScroll = 0;
    // Editing a text cell in place.
    bool                           m_editing = false;
    int                            m_editRow = -1, m_editColumn = -1;
    JTextEditCore                  m_edit;
    std::unique_ptr<JMenu>         m_choiceMenu;
};

} // inline namespace jf
