// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPFieldsModel.h"
#include "JPTableHistory.h"
#include "JPTableModel.h"

#include <j/config/Json.h>
#include <j/core/JComboBox.h>
#include <j/core/JControl.h>
#include <j/core/JTextEditCore.h>
#include <j/core/MenuSystem.h>

#include <functional>
#include <map>
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
//    case anywhere in any column, as OpenPnP's search boxes do; "name: text"
//    (a column's heading before a colon) in that column alone, "name:" alone
//    the rows with anything in it. What matched is marked in each cell; a row
//    found only in a column not shown is marked at its start, and its tooltip
//    says which.
//  - SELECTION: click, Shift+click a range, Ctrl+click one more; the arrows,
//    Home, End, Page Up and Down move it (Shift extends); Ctrl+A all.
//  - EDITING: a text cell is edited on a double-click, F2 or by typing (all
//    its text chosen first); Return or Tab keeps it, Escape puts it back. A
//    tick box changes on a click; a choice opens its menu on a click.
//  - Ctrl+C copies the chosen rows, a tab between cells. Ctrl+V pastes such
//    cells from the cell chosen, down and along (one value into every row
//    chosen); one a cell cannot take, and nothing is pasted. A cell that
//    cannot be changed is passed over.
//  - MANY ROWS: a value typed, a tick or a choice in a row of several chosen
//    goes into all of them. Each change is one step of the table edits'
//    history (s_history; undone with Edit ▸ Undo), but where the model has a
//    history of its own (ownHistory); and said in the status (s_status).
//  - A column's heading shows its tooltip; drag a heading's edge to widen it.
//  - COLUMNS: drag a heading sideways to move its column; while it is
//    dragged the table shows how it will look (the column floating with the
//    pointer, the others moved aside to make its place), letting go keeps
//    it, Escape puts every column back. A press that does not move sorts.
//    Right-click a heading: Reset Columns (the order, widths and sort as
//    shipped). A column can be hidden (columnsMenu, a toolbar's Columns…)
//    and shown again in its place. With a state key, the order, widths, sort
//    and hidden columns are kept (by the columns' keys) and brought back,
//    every table alike.
//  - VIEWS: a table's layouts by name (its views), each its own order,
//    widths, sort and columns shown, the one shown changed as it is used;
//    switched in one click (a toolbar's view buttons, Columns…, Ctrl+1 to
//    Ctrl+9). Standard (as shipped) and All Fields (every column, always,
//    those added later too) to begin with; more made from the one shown.
//    A view locked keeps what it was made as: a change while it is shown
//    lasts until another is shown or it is put back (Revert View). A view
//    may keep its search, shown with it.
//  - FIELDS: a model whose rows have fields (JPTableModel::fieldsOf) is shown
//    with them (JPFieldsModel over it, the app's fields, s_fields): a column
//    for each field added, none for a built-in removed. The Columns… menu and
//    a heading's right-click add a field (shown here, hidden in the other
//    tables of its kind until chosen) and edit them (s_fieldHooks).
//
// Rows are the model's: what the table reports and is told is always a
// model row, whatever the sort.
class JPTable : public JControl {
public:
    explicit JPTable(JSceneGraph& graph);

    // OpenPnP's Alternate Rows (Change Appearance): every other row shaded, in every table.
    static void setAlternateRows(bool on) { s_alternateRows = on; }
    static bool alternateRows() { return s_alternateRows; }

    void setModel(JPTableModel* model);
    // The model shown: the one given, or its fields' over it.
    JPTableModel* model() const { return m_model; }

    // The app's fields, every table's (null: none, the models shown as they are).
    static inline JPFields* s_fields = nullptr;
    // The app's windows for fields: adding one to a kind (named `name` to begin with, matched by it as a heading;
    // `added` told its key, once added), editing one (its key), and the kind's every field (Edit Fields…).
    struct FieldHooks {
        std::function<void(JPFieldSet::Of, const std::string& name, std::function<void(const std::string& key)> added)> add;
        std::function<void(JPFieldSet::Of, const std::string& key)>                           edit;
        std::function<void(JPFieldSet::Of)>                                                   manage;
    };
    static inline FieldHooks s_fieldHooks;
    // Fields changed (added, renamed, removed): every table shows them as they are now.
    static void fieldsChanged();

    // The app's history of what is changed in tables, and its status line (a change of many rows said).
    static inline JPTableHistory*                         s_history = nullptr;
    static inline std::function<void(const std::string&)> s_status;
    static bool alive(const JPTable* t) { return s_tables.count(const_cast<JPTable*>(t)) > 0; }
    // The cells set back to their values before a change (undo), or again to those after it (redo), found by their
    // keys; one change of the model's, no step made.
    void putCells(const std::vector<JPTableHistory::Cell>& cells, bool before);
    // The clipboard's cells pasted from the chosen cell (Ctrl+V).
    void paste();
    // Where its columns' order, widths and sort are kept (the app's hooks, s_loadState and s_saveState): brought
    // back now (and when the model's columns change), kept as they are changed. Every table has one of its own.
    void setStateKey(const std::string& key);
    static inline std::function<JJson(const std::string& key)>                  s_loadState;
    static inline std::function<void(const std::string& key, const JJson& state)> s_saveState;
    // The columns as shipped: the model's order, widths shared out, no sort, every one shown.
    void resetColumns();

    // Its views (VIEWS above): what each is called, the icon its button shows (JPViewIcons), whether it shows
    // every column always (All Fields), is locked, keeps its search (and which); its layout as kept.
    struct View {
        std::string name;
        std::string icon;
        bool        all = false;
        bool        locked = false;
        bool        keepSearch = false;
        std::string search;
        JJson       layout;
    };
    const std::vector<View>& views() const { return m_views; }
    int  currentView() const { return m_current; }
    // The one shown changed since it was shown (a locked one, Revert View puts it back).
    bool viewChanged() const { return m_viewChanged; }
    void showView(int i);
    // A view made from the one shown (its layout, a free icon), shown; its index.
    int  addView(const std::string& name);
    void renameView(int i, const std::string& name);
    void setViewIcon(int i, const std::string& icon);
    int  duplicateView(int i);
    // Taken away (the last one stays).
    void deleteView(int i);
    // `from` moved to before `to` (views().size(): the end).
    void moveView(int from, int to);
    void setViewLocked(int i, bool on);
    void setViewKeepsSearch(int i, bool on);
    // The one shown put back as it was kept (a locked one's changes undone).
    void revertView();
    // The column `key` shown in every view (All Fields shows it anyway).
    void showInEveryView(const std::string& key);
    // The menu of a view's own actions (rename, icon, duplicate, lock, keep search, delete; a view button's
    // right-click), made afresh each time it is asked for.
    JMenu* viewMenu(int i);
    // The views changed: added, taken away, renamed, moved, shown (a toolbar's view buttons made again).
    jf::JSignal<> onViewsChanged;
    // A view shown with its search: what the table's search line is to show.
    jf::JSignal<std::string> onSearchShown;
    // Given a toolbar's tools (JPTableTools: its view buttons): its menus offer its views and its fields. A table
    // without (a chooser's, a dialog's) is a plain one: its columns shown and hidden, nothing more.
    void setHasTools(bool on) { m_hasTools = on; }
    bool hasTools() const { return m_hasTools; }
    // Whether its menus offer its fields to add and edit (one with tools, but a read-only list's: a dropdown).
    void setOffersFields(bool on) { m_offersFields = on; }
    // The table last focused (Ctrl+1 to 9 switch its view).
    static JPTable* focused() { return s_focusedTable; }
    // Which columns are shown: each of the model's, by name, in its place among them (a hidden one keeps its
    // place and width for when it is shown again).
    struct ColumnShown {
        std::string key;    // what the column is for good (JPTableModel::keyOf)
        std::string name;   // its heading
        bool        shown = true;
    };
    std::vector<ColumnShown> columnsShown() const;
    // The column `key` (JPTableModel::keyOf) shown or hidden, kept with the rest; the last one shown stays.
    void setColumnShown(const std::string& key, bool shown);
    // A toolbar's Columns… menu: each column with a tick when it is shown (a click shows or hides it), then
    // Reset Columns. Made afresh each time it is asked for.
    JMenu* columnsMenu();
    // The model's rows changed: sorted and filtered again, the chosen rows
    // kept (by JPTableModel::rowKey).
    void refresh();

    void setFilter(const std::string& regex);   // empty: every row
    // The search taken off, its field emptied with it (onSearchShown).
    void clearSearch();
    const std::string& filter() const { return m_filterText; }
    // Whether a search is on; the rows it leaves (viewRowCount) out of those there would be without it.
    bool searching() const { return m_filter.has_value() || !m_filterKey.empty(); }
    int  unsearchedRowCount() const;
    // Every column's heading (the hidden ones' too, an icon's by what it is), as a search names them.
    std::vector<std::string> columnNames() const;
    // The rows shown changed (sorted, searched, made again).
    jf::JSignal<> onViewChanged;

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

    // Without the headings (a list, as OpenPnP's log): not sorted by a click then.
    void setHeaderShown(bool on) { m_headerShown = on; }
    // The last row brought into view; whether it is in view now.
    void scrollToEnd();
    bool atEnd() const;
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
    ~JPTable() override;

private:
    static inline bool s_alternateRows = true;
    // Every table there is, for fieldsChanged.
    static inline std::set<JPTable*> s_tables;
    static inline JPTable* s_focusedTable = nullptr;
    // The views as kept with the table's state (none kept: Standard, from a layout kept before views, and All
    // Fields); the whole of it as kept.
    void readViews(const JJson& state);
    void saveViews() const;
    // The current view's layout taken into the order, widths and sort (made again).
    void takeLayout(const JJson& layout);
    // An icon no view has yet (the first, when every one is used).
    std::string freeIcon() const;
    // A view's name asked for, then `then` with it (JDialog::input; empty: nothing).
    void askViewName(const std::string& title, const std::string& now, std::function<void(const std::string&)> then);
    // A field added here: shown here (hidden in the other tables of its kind), after the column `after`
    // (a key; empty: at the end).
    void fieldAdded(const std::string& key, const std::string& after);
    // The headings' right-click menu for the column under `mx` (-1: none).
    void buildHeaderMenu(int column);
    // A cell's value as an edit gives it (a tick "true" or "false", else its text), and set from one (false, and
    // why, when it is refused).
    std::string cellValue(int row, int c) const;
    bool        setCellValue(int row, int c, const std::string& value, std::string& error);
    // Cells set as one change: the model's edits begun and ended, a step of the history, the change said. A
    // value refused: when `atomic`, every cell put back as it was and nothing kept; else that one left. What
    // was refused and why in `refused`; how many changed in `changed`.
    struct CellSet {
        int         row, column;
        std::string value;
    };
    bool setCells(const std::vector<CellSet>& sets, bool atomic, std::vector<std::string>& refused, int& changed);
    // A tick turned over, in every row chosen with it.
    void toggle(int row, int c);
    // The rows an edit of `row` goes to: the rows chosen, in their order, when it is one of several; else it.
    std::vector<int> editRows(int row) const;
    // A row's model index by its key, or -1; a column's by its key, or -1.
    int rowByKey(const std::string& key) const;
    int columnByKey(const std::string& key) const;
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
    // The model's columns in the order shown (made again, and the kept state taken, when the model's columns
    // change), and a column's place in it.
    const std::vector<int>& order() const;
    // Every column in its place, the hidden ones put back among those shown.
    std::vector<int> allColumns() const;
    int   positionOf(int c) const;
    // The kept state taken into the order, widths and sort; the state as it is now, kept.
    void  applyState() const;
    void  applySort();
    void  keepState() const;
    // The layout as it is now (order, hidden, widths, sort), as kept.
    JJson layoutNow() const;
    // A heading dragged: the order its place would give; the drag ended (kept or put back).
    void  previewDrop(float mx);
    void  endColumnDrag(bool keep);
    int   columnAt(float mx) const;
    int   dividerAt(float mx, float my) const;
    int   viewRowAt(float my) const;
    // Rows can be dragged: the model says so, the view in its order, every row shown.
    bool  canDragRows() const;
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
    std::unique_ptr<JPFieldsModel> m_fieldsModel;     // the model given, with its fields
    // Fields added here, to be shown (and where) once the columns are made again.
    mutable std::vector<std::pair<std::string, std::string>> m_addedHere;
    std::string                    m_stateKey;
    mutable JJson                  m_state;           // the layout shown: order, widths and sort by the columns' keys
    mutable std::vector<View>      m_views;   // the one shown's layout kept as it changes (keepState)
    int                            m_current = 0;
    mutable bool                   m_viewChanged = false;   // the one shown changed, locked: not kept
    std::unique_ptr<JMenu>         m_viewMenu;
    bool                           m_hasTools = false;
    bool                           m_offersFields = true;
    std::unique_ptr<JMenu>         m_iconMenu;
    mutable std::vector<int>       m_order;           // view position -> model column (the columns shown)
    mutable std::vector<int>       m_allOrder;        // every column in its place, the hidden ones too, as last made
    mutable std::set<std::string>  m_hidden;          // the hidden columns, by key
    mutable std::vector<std::string> m_orderKeys;     // the model's columns' keys m_order was made for
    // A heading pressed (a click sorts it) and, moved far enough, dragged: the column, where in it it was taken,
    // the pointer, and the order to put back.
    int                            m_headerPress = -1;
    float                          m_headerPressX = 0;
    bool                           m_columnDrag = false;
    float                          m_grabOffset = 0, m_dragX = 0;
    std::vector<int>               m_orderBefore;
    // The headings' right-click menu (Reset Columns), and the rows' (the owner's) it stands in for there.
    std::unique_ptr<JMenu>         m_headerMenu;
    std::unique_ptr<JMenu>         m_columnsMenu;
    JMenu*                         m_rowsMenu = nullptr;
    bool                           m_headerShown = true;
    std::vector<int>               m_view;            // view index -> model row
    std::vector<std::string>       m_keys;            // model row -> its key, as last built
    std::vector<SortKey>           m_sortKeys;        // first is the primary
    std::string                    m_filterText;
    std::optional<std::regex>      m_filter;
    std::string                    m_filterKey;       // the one column searched (its key); empty: every column
    // Rows found only in a column not shown: that column's heading.
    mutable std::map<int, std::string> m_hiddenHits;
    std::set<int>                  m_selected;        // model rows
    int                            m_lead = -1;       // model row
    int                            m_leadColumn = 0;
    int                            m_anchor = -1;     // model row a Shift range runs from
    // A plain click on a row of several chosen: it alone chosen as the click is released (-1: none); and the
    // rows chosen before that, for a double-click's second press.
    int                            m_collapseOnRelease = -1;
    std::set<int>                  m_beforeCollapse;
    mutable std::vector<float>     m_widths;
    float                          m_scrollX = 0, m_scrollY = 0;
    // A model row to bring into view once the table has its height (chosen while hidden).
    int                            m_reveal = -1;
    int                            m_resizing = -1;
    float                          m_resizeFromX = 0, m_resizeFromW = 0;
    bool                           m_draggingV = false;
    float                          m_dragFromY = 0, m_dragFromScroll = 0;
    // A row dragged to another place: the model row, where the press was,
    // and (once moved far enough) the row it goes before.
    int                            m_rowDrag = -1;
    float                          m_rowDragFromY = 0;
    int                            m_dropBefore = -1;
    // Editing a text cell in place.
    bool                           m_editing = false;
    int                            m_editRow = -1, m_editColumn = -1;
    float                          m_editScroll = 0;   // how far the edited text is scrolled left (px)
    JTextEditCore                  m_edit;
    // A Choice cell's list: a combo's dropdown over the cell (as many choices as there are, scrolled past
    // a dropdown's worth), not a menu of them all (a long list ran off the screen).
    std::unique_ptr<JComboBox>     m_choiceCombo;
};

} // inline namespace jf
