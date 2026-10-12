// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include <j/core/JContainer.h>
#include <j/core/MenuSystem.h>

#include <functional>
#include <memory>
#include <vector>

inline namespace jf {

class JPIconButton;
class JPTable;

// A table's tools at the end of its toolbar (JPUiParts::tableTools), after a divider: Columns… (its columns
// menu: the views, the fields, the columns shown) and a button for each of its views (JPTable's views), its
// icon (JPViewIcons), its name its tooltip, the one shown lit and a dot on it while a locked one is changed;
// a click shows it, a right-click opens its own menu (JPTable::viewMenu), and dragged along the row it moves
// there. Made again as the views change.
class JPTableTools : public JContainer {
public:
    // `open`: the panel's way of opening a menu at window coordinates (read when clicked).
    JPTableTools(JSceneGraph& graph, const std::function<void(JMenu*, float x, float y)>* open);
    ~JPTableTools() override;

    // The table it is the tools of, once it is made.
    void attach(JPTable& table);
    JPTable* table() const { return m_table; }

    JPIconButton* columnsButton() const { return m_columns; }

private:
    class ViewButton;
    void rebuild();
    // The view a window x falls in, as the buttons lie now (to drop a dragged one before): the index of the
    // first whose middle is right of it, or the count.
    int  dropIndex(float x) const;

    const std::function<void(JMenu*, float, float)>* m_open = nullptr;
    JPTable*                                         m_table = nullptr;
    JPIconButton*                                    m_columns = nullptr;
    std::vector<ViewButton*>                         m_buttons;
    std::shared_ptr<bool>                            m_alive = std::make_shared<bool>(true);
    bool                                             m_rebuildPosted = false;
};

} // inline namespace jf
