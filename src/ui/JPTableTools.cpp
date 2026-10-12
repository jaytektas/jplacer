// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPTableTools.h"

#include "JPIconButton.h"
#include "JPTable.h"
#include "JPViewIcons.h"

#include <j/core/FrameTimer.h>
#include <j/core/JSeparator.h>
#include <j/core/JStyle.h>

#include <cmath>

inline namespace jf {

// A view's button: shows it on a click, its own menu on a right-click, moves it when dragged along the row.
class JPTableTools::ViewButton : public JPIconButton {
public:
    ViewButton(JSceneGraph& graph, JPTableTools& tools, int view, const JPTable::View& v, bool shown, bool changed)
        : JPIconButton(graph, v.name, JPViewIcons::glyph(v.icon),
                       v.name + (changed ? " (changed: Revert View puts it back)" : "") + (view < 9 ? " (Ctrl+" + std::to_string(view + 1) + ")" : "") +
                           ": a view of this table; right-click for its menu, drag to move it"),
          m_tools(tools), m_view(view), m_changed(changed) {
        setChecked(shown);
        onClicked.connect([this] {
            if (JPTable* t = m_tools.table()) t->showView(m_view);
        });
    }

    void prepareContextMenu(float, float) override {
        if (JPTable* t = m_tools.table()) setContextMenu(t->viewMenu(m_view));
    }
    void handleMousePress(float mx, float my) override {
        m_pressX = mx;
        m_dragging = false;
        JPIconButton::handleMousePress(mx, my);
    }
    void handleMouseMove(float mx, float my) override {
        if (m_armed && !m_dragging && std::fabs(mx - m_pressX) > JStyle::current().doubleClickSlop) m_dragging = true;
        JPIconButton::handleMouseMove(mx, my);
    }
    void handleMouseRelease(float mx, float my) override {
        if (m_dragging && m_armed) {
            // Dropped: no click; the view moved to where it was let go.
            m_armed = false;
            m_dragging = false;
            setState(JWidgetState::Normal);
            const int to = m_tools.dropIndex(mx);
            if (JPTable* t = m_tools.table()) t->moveView(m_view, to);
            return;
        }
        JPIconButton::handleMouseRelease(mx, my);
    }
    void populateRenderPrimitives(JPrimitiveBuffer& buf) override {
        JPIconButton::populateRenderPrimitives(buf);
        if (!m_changed) return;
        // A locked view changed: a dot in its top right corner.
        const JRect b = m_graph.getLayoutConst(m_nodeId).boundingBox;
        const float d = std::max(2.f, b.width * 0.22f);
        buf.pushRectangle(b.x + b.width - d - 1.f, b.y + 1.f, d, d, isChecked() ? Colors::HighlightedText : Colors::Accent, d * 0.5f);
    }

private:
    JPTableTools& m_tools;
    int           m_view;
    bool          m_changed;
    float         m_pressX = 0;
    bool          m_dragging = false;
};

JPTableTools::JPTableTools(JSceneGraph& graph, const std::function<void(JMenu*, float, float)>* open)
    : JContainer(graph, 0.f, 0.f), m_open(open) {
    const JStyle& st = JStyle::current();
    setDirection(JFlexDirection::JRow)->setGap(st.spacing)->setAlignItems(JAlignItems::Center);
    setHSizePolicy(JSizePolicyMode::Fixed);
    rebuild();
}

JPTableTools::~JPTableTools() { *m_alive = false; }

void JPTableTools::attach(JPTable& table) {
    m_table = &table;
    // Made again on the next frame, not inside the click (a button's own signal) that changed the views.
    table.onViewsChanged.connect([this] {
        if (m_rebuildPosted) return;
        m_rebuildPosted = true;
        std::weak_ptr<bool> alive = m_alive;
        jPostToNextFrame([this, alive] {
            if (const auto a = alive.lock(); !a || !*a) return;
            m_rebuildPosted = false;
            rebuild();
        });
    });
    rebuild();
}

void JPTableTools::rebuild() {
    JSceneGraph& g = m_graph;
    const float icon = JPIconButton::size();
    const JStyle& st = JStyle::current();
    clear();
    m_buttons.clear();
    add(std::make_unique<JSeparator>(g, JSeparator::JOrientation::Vertical, icon));
    m_columns = add(std::make_unique<JPIconButton>(
        g, "Columns", "table-columns",
        "This table's views, the fields of what its rows are, and the columns it shows (each table keeps its own)"));
    m_columns->setLeads(JPIconButton::Leads::Menu);
    m_columns->onClicked.connect([this] {
        if (!m_table || !m_open || !*m_open) return;
        const JRect r = m_columns->bounds();
        (*m_open)(m_table->columnsMenu(), r.x, r.y + r.height);
    });
    int count = 2;
    if (m_table)
        for (size_t i = 0; i < m_table->views().size(); ++i) {
            const bool shown = int(i) == m_table->currentView();
            m_buttons.push_back(add(std::make_unique<ViewButton>(g, *this, int(i), m_table->views()[i], shown,
                                                                 shown && m_table->viewChanged())));
            ++count;
        }
    // As wide as its separator and buttons.
    setSize(float(count) * icon + float(count - 1) * st.spacing, icon);
    setMinimumSize(float(count) * icon + float(count - 1) * st.spacing, icon);
    invalidate();
}

int JPTableTools::dropIndex(float x) const {
    for (size_t i = 0; i < m_buttons.size(); ++i) {
        const JRect r = m_buttons[i]->bounds();
        if (x < r.x + r.width * 0.5f) return int(i);
    }
    return int(m_buttons.size());
}

} // inline namespace jf
