// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPSetupForm.h"

#include "JPGroupFrame.h"
#include "JPIconButton.h"
#include "JPIcons.h"
#include "JPImageBox.h"
#include "JPPlotView.h"
#include "JPSearchStrip.h"
#include "JPTextBox.h"
#include "JPTextField.h"
#include "JPUiParts.h"

#include <j/core/JComboBox.h>
#include <j/core/FrameTimer.h>
#include <j/core/JButton.h>
#include <j/core/JColorButton.h>
#include <j/core/JSlider.h>
#include <j/core/JLabel.h>
#include <j/core/JPropertyBinding.h>
#include <j/core/JScrollArea.h>
#include <j/core/JSeparator.h>
#include <j/core/JStyle.h>
#include <j/core/JTextHelper.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

inline namespace jf {

namespace {

// No limit on a size (the framework's own ceiling).
constexpr float kUnbounded = 1.0e6f;
// A graph is this many lines tall.
constexpr float kPlotLines = 14;
// A picture's box is this many label lines square.
constexpr float kImageLines = 8;

using Row = JPSetupProperties::Row;

// A number as wide as one with its steppers; a name twice that.
float numberWidth() {
    const JStyle& st = JStyle::current();
    return JTextHelper::measureWidth("-00000.000") + 2 * st.fieldPadding + st.controlHeight;
}

float rowHeight() {
    const JStyle& st = JStyle::current();
    return std::max({ st.buttonHeight, st.controlHeight, JPIconButton::size() });
}

// How tall a box of lines is for `p`: as many lines as its value, at least one.
float linesHeight(const JProperty& p) {
    const std::string v = p.get().toString();
    int lines = 1;
    for (char c : v) lines += c == '\n';
    return JPTextBox::heightFor(lines);
}

// A row in the columns under a header: a place, or several settings side by
// side. A row of one setting (a diameter under a place) is laid out as it is.
bool inGrid(const Row& r) {
    return r.place != JPSetupProperties::Place::None || r.cells.size() > 1;
}

bool isText(const JProperty& p) {
    if (!p.meta.choices.empty()) return false;
    const JVariant v = p.get();
    return !v.isBool() && !v.isInt() && !v.isDouble();
}

// A fixed-width box holding `w`, set at its start, centre or end.
std::unique_ptr<JContainer> box(JSceneGraph& graph, float width, float height, JJustifyContent at) {
    auto b = std::make_unique<JContainer>(graph, width, height);
    b->setDirection(JFlexDirection::JRow)->setAlignItems(JAlignItems::Center);
    graph.getLayout(b->getNodeId()).justifyContent = at;
    b->setHSizePolicy(JSizePolicyMode::Fixed);
    b->setFixedSize(width, height);
    return b;
}

std::unique_ptr<JLabel> label(JSceneGraph& graph, const std::string& text) {
    const JStyle& st = JStyle::current();
    auto l = std::make_unique<JLabel>(graph, text, 0.f, st.labelHeight);
    // Its text's width, and a little: the text is cut at the label's edge.
    l->setFixedSize(std::ceil(JTextHelper::measureWidth(tr(text))) + st.spacing, st.labelHeight);
    return l;
}

} // namespace

JPSetupForm::JPSetupForm(JSceneGraph& graph) : JContainer(graph, 0.f, 0.f) {
    setDirection(JFlexDirection::Column)->setAlignItems(JAlignItems::Stretch);
    m_tabs = add(std::make_unique<JTabWidget>(graph, 0.f, 0.f));
    m_tabs->setTabWrap(true);   // narrow, the tabs go onto more rows, none out of sight
    m_tabs->setVSizePolicy(JSizePolicyMode::Expanding, 1);
    m_single = add(std::make_unique<JContainer>(graph, 0.f, 0.f));
    m_single->setDirection(JFlexDirection::Column)->setAlignItems(JAlignItems::Stretch);
    m_single->setVSizePolicy(JSizePolicyMode::Fixed);
    m_single->setFixedSize(0.f, 0.f);
}

void JPSetupForm::attachPages(int active) {
    while (m_tabs->tabCount() > 0) m_tabs->removeTab(m_tabs->tabCount() - 1);
    m_single->clear();
    // One page, shown where its tab bar would only repeat its title: on its own.
    const bool alone = !m_singleTabBar && m_pages.size() == 1;
    // The one shown takes the room; the other none.
    JWidget* shown = alone ? static_cast<JWidget*>(m_single) : m_tabs;
    JWidget* hidden = alone ? static_cast<JWidget*>(m_tabs) : m_single;
    hidden->setFixedSize(0.f, 0.f);
    shown->setMinimumSize(0.f, 0.f);
    shown->setMaximumSize(kUnbounded, kUnbounded);
    shown->setHSizePolicy(JSizePolicyMode::Expanding, 1);
    shown->setVSizePolicy(JSizePolicyMode::Expanding, 1);
    if (alone) {
        // Laid out as a child here, not placed by a tab widget: it must ask for the room.
        m_pages.front()->setHSizePolicy(JSizePolicyMode::Expanding, 1);
        m_pages.front()->setVSizePolicy(JSizePolicyMode::Expanding, 1);
        m_single->add(m_pages.front().get());
    } else {
        for (size_t i = 0; i < m_pages.size(); ++i) m_tabs->addTab(m_form.tabs[i].title, m_pages[i].get());
        if (!m_pages.empty()) m_tabs->setActiveTab(std::clamp(active, 0, int(m_pages.size()) - 1));
    }
    invalidate();
}

void JPSetupForm::setForm(JPSetupProperties::Form form) {
    // The same tab as before, when this part has one of that name.
    const int was = m_tabs->activeTab();
    const std::string wasTitle = was >= 0 && was < int(m_form.tabs.size()) ? m_form.tabs[size_t(was)].title : "";
    while (m_tabs->tabCount() > 0) m_tabs->removeTab(m_tabs->tabCount() - 1);
    m_single->clear();
    m_contents.clear();
    m_pages.clear();
    m_pulls.clear();
    m_form = std::move(form);
    m_builtWidth = m_graph.getLayoutConst(getNodeId()).boundingBox.width;
    int active = 0;
    for (size_t i = 0; i < m_form.tabs.size(); ++i) {
        m_pages.push_back(page(m_form.tabs[i]));
        if (m_form.tabs[i].title == wasTitle) active = int(i);
    }
    attachPages(active);
}

void JPSetupForm::fitPages() {
    // A page is as tall as its groups reach, as they were laid out: a note
    // wrapped to more lines than foreseen must not be cut off, nor what is
    // under it.
    const JStyle& st = JStyle::current();
    for (JContainer* c : m_contents) {
        const JRect cb = m_graph.getLayoutConst(c->getNodeId()).boundingBox;
        float bottom = cb.y;
        for (NodeId k : m_graph.getChildren(c->getNodeId())) {
            const JRect kb = m_graph.getLayoutConst(k).boundingBox;
            bottom = std::max(bottom, kb.y + kb.height);
        }
        const float need = bottom - cb.y + st.spacing;
        if (need > cb.height + 0.5f) {
            c->setSize(cb.width, need);
            invalidate();
        }
    }
}

void JPSetupForm::populateRenderPrimitives(JPrimitiveBuffer& buf) {
    fitPages();
    // A new width: the pages made again to it, on the next frame (not while drawing).
    const float w = m_graph.getLayoutConst(getNodeId()).boundingBox.width;
    if (!m_pages.empty() && !m_rebuilding && std::abs(w - m_builtWidth) > 1.f) {
        m_rebuilding = true;
        std::weak_ptr<bool> alive = m_alive;
        jPostToNextFrame([this, alive] {
            if (!alive.lock()) return;
            m_rebuilding = false;
            rebuild();
        });
    }
    JContainer::populateRenderPrimitives(buf);
}

void JPSetupForm::rebuild() {
    const int active = m_tabs->activeTab();
    std::vector<float> scrolled;
    for (const auto& p : m_pages)
        if (const auto* s = dynamic_cast<const JScrollArea*>(p.get())) scrolled.push_back(s->scrollY());
    while (m_tabs->tabCount() > 0) m_tabs->removeTab(m_tabs->tabCount() - 1);
    m_single->clear();
    m_pages.clear();
    m_pulls.clear();
    m_contents.clear();
    m_builtWidth = m_graph.getLayoutConst(getNodeId()).boundingBox.width;
    for (size_t i = 0; i < m_form.tabs.size(); ++i) {
        m_pages.push_back(page(m_form.tabs[i]));
        if (auto* s = dynamic_cast<JScrollArea*>(m_pages.back().get()); s && i < scrolled.size()) s->setScrollY(scrolled[i]);
    }
    attachPages(active);
}

void JPSetupForm::remake(JPSetupProperties::Form form) {
    m_form = std::move(form);
    rebuild();
}

void JPSetupForm::refresh() {
    m_pulling = true;
    for (const auto& pull : m_pulls) pull();
    m_pulling = false;
}

const JProperty* JPSetupForm::find(const std::string& name) const {
    for (const JProperty& p : m_form.model.all())
        if (p.name == name) return &p;
    return nullptr;
}

bool JPSetupForm::set(const std::string& property, const JVariant& value) {
    const JProperty* p = find(property);
    if (!p || !p->writable()) return false;
    const bool ok = p->set(value);
    refresh();
    return ok;
}

JVariant JPSetupForm::get(const std::string& property) const {
    const JProperty* p = find(property);
    return p ? p->get() : JVariant();
}

std::unique_ptr<JWidget> JPSetupForm::page(const JPSetupProperties::Tab& tab) {
    const JStyle& st = JStyle::current();
    auto scroll = std::make_unique<JScrollArea>(m_graph, 0.f, 0.f);
    auto content = std::make_unique<JContainer>(m_graph, 0.f, 0.f);
    content->setDirection(JFlexDirection::Column)->setGap(2 * st.spacing)->setAlignItems(JAlignItems::Stretch);
    // Clear of the scroll bar and the page's foot, so the groups' frames show whole.
    content->setPadding(JEdges(0.f, 0.f, st.scrollBarWidth + st.spacing, st.spacing));
    // One column of labels for the whole page, so every group's controls line up.
    float labels = 0;
    for (const JPSetupProperties::Group& g : tab.groups)
        for (const Row& r : g.rows)
            if (r.kind == Row::Kind::Fields) labels = std::max(labels, std::ceil(JTextHelper::measureWidth(tr(r.label))) + st.spacing);
    float height = 0;
    for (const JPSetupProperties::Group& g : tab.groups) {
        float h = 0;
        content->add(group(g, h, labels));
        height += (height > 0 ? 2 * st.spacing : 0) + h;
    }
    // As tall as its groups: it sits in a scroll area, which goes by its children's heights.
    // (Made taller still if they come out taller once laid out: fitPages.)
    content->setVSizePolicy(JSizePolicyMode::Fixed);
    content->setSize(m_graph.getLayoutConst(content->getNodeId()).boundingBox.width, height + st.spacing);
    m_contents.push_back(content.get());
    scroll->addChildWidget(std::move(content));
    return scroll;
}

float JPSetupForm::widthOf(const JProperty& p) const {
    const JStyle& st = JStyle::current();
    // Shown text: as wide as it is drawn, with room to spare (a measure falls a little short of it), and
    // no narrower than a number's field, as editor() makes it.
    if (!p.writable()) return std::max(std::ceil(JTextHelper::measureWidth(p.get().toString())) + st.spacing, numberWidth());
    if (!p.meta.choices.empty()) {
        // The longest item, its padding either side, and the arrow.
        float widest = 0;
        for (const JVariant& c : p.meta.choices) widest = std::max(widest, JTextHelper::measureWidth(tr(c.toString())));
        const float fits = std::ceil(widest) + 2 * st.fieldPadding + st.controlHeight + 2 * st.spacing;
        // One typed into has a text field's room too.
        return p.meta.editor == "editable-choice" ? std::max(fits, 2 * numberWidth()) : fits;
    }
    const JVariant v = p.get();
    if (v.isBool()) return st.checkHeight;
    if (v.isDouble() && p.meta.decimals > 0) {
        // Room for all its places (a coefficient's many), its padding and its spin buttons.
        char text[64];
        std::snprintf(text, sizeof text, "%.*f", p.meta.decimals, v.toDouble());
        const float fits = std::ceil(JTextHelper::measureWidth(text)) + 2 * st.fieldPadding + st.controlHeight + 2 * st.spacing;
        return std::max(fits, numberWidth());
    }
    if (v.isInt() || v.isDouble() || p.meta.editor == "number") return numberWidth();
    return 2 * numberWidth();   // a name, a line of text ("long" ones take the row's room)
}

std::unique_ptr<JWidget> JPSetupForm::editor(const JProperty& p, float width) {
    const JStyle& st = JStyle::current();
    if (!p.writable()) {
        // As wide as its text with room to spare, and no narrower than a number's field (it may grow).
        auto value = std::make_unique<JLabel>(m_graph, p.get().toString(), 0.f, st.labelHeight);
        JLabel* v = value.get();
        v->setMinWidthFollowsText(true);
        v->setHSizePolicy(JSizePolicyMode::Fixed);
        v->setFixedSize(std::max(width, numberWidth()), st.labelHeight);
        m_pulls.push_back([v, get = p.get] { v->setText(get().toString()); });
        return value;
    }
    // Each edit goes through the setter, then is reported; a control set by
    // refresh() is not an edit.
    JProperty bound = p;
    bound.set = [this, set = p.set, name = p.name](const JVariant& v) {
        if (m_pulling) return true;
        const bool ok = set(v);
        if (ok && onChanged) onChanged(name);
        return ok;
    };
    JPropertyEditor e;
    if (isText(p) && p.meta.editor == "lines") {
        // Lines of text: as tall as they are, committed on leaving.
        auto box = std::make_unique<JPTextBox>(m_graph, p.meta.def);
        JPTextBox* b = box.get();
        b->onCommitted.connect([set = bound.set](const std::string& t) { set(JVariant(t)); });
        e.pull = [b, get = p.get] { b->setValue(get().toString()); };
        e.widget = std::move(box);
        e.widget->setHSizePolicy(JSizePolicyMode::Expanding, 1);
        e.widget->setVSizePolicy(JSizePolicyMode::Fixed);
        e.widget->setSize(m_graph.getLayoutConst(e.widget->getNodeId()).boundingBox.width, linesHeight(p));
        m_pulling = true;
        e.pull();
        m_pulling = false;
        m_pulls.push_back(e.pull);
        return std::move(e.widget);
    }
    if (p.meta.editor == "slider") {
        // Whole numbers from its least to its most along it; each move an edit.
        const int lo = int(p.meta.min.toInt()), hi = std::max(lo + 1, int(p.meta.max.toInt()));
        auto slider = std::make_unique<JSlider>(m_graph, 0.f, st.controlHeight);
        JSlider* sl = slider.get();
        sl->onValueChanged.connect([set = bound.set, lo, hi](float v) {
            set(JVariant(lo + int(std::lround(v * float(hi - lo)))));
        });
        e.pull = [sl, get = p.get, lo, hi] { sl->setValue(float(get().toInt() - lo) / float(hi - lo)); };
        e.widget = std::move(slider);
        e.widget->setHSizePolicy(JSizePolicyMode::Expanding, 1);
        e.widget->setVSizePolicy(JSizePolicyMode::Fixed);
        m_pulling = true;
        e.pull();
        m_pulling = false;
        m_pulls.push_back(e.pull);
        if (!p.meta.tooltip.empty()) e.widget->setTooltip(p.meta.tooltip);
        return std::move(e.widget);
    }
    if (p.meta.editor == "editable-choice") {
        // A choice that may also be typed: the choices offered, any name kept (on Return or choosing).
        std::vector<std::string> items;
        for (const JVariant& c : p.meta.choices) items.push_back(c.toString());
        auto combo = std::make_unique<JComboBox>(m_graph, items);
        JComboBox* c = combo.get();
        c->setEditable(true);
        c->onTextChanged.connect([set = bound.set](const std::string& t) { set(JVariant(t)); });
        e.pull = [c, get = p.get] { c->setEditText(get().toString()); };
        e.widget = std::move(combo);
    } else     if (p.meta.editor == "color") {
        // The swatch; a click opens the colour chooser.
        auto button = std::make_unique<JColorButton>(m_graph, 2 * numberWidth(), st.controlHeight);
        JColorButton* b = button.get();
        b->onColorChanged.connect([set = bound.set](const std::string& hex) { set(JVariant(hex)); });
        e.pull = [b, get = p.get] { b->setColorHex(get().toString()); };
        e.widget = std::move(button);
    } else if (isText(p)) {
        // A line of text changes when it is committed (Return, Tab, leaving
        // it), as a number does; the framework's editor for it changes on each key.
        auto field = std::make_unique<JPTextField>(m_graph);
        JPTextField* f = field.get();
        // What an empty field stands for, greyed in it.
        if (!p.meta.def.empty()) f->setPlaceholderText(p.meta.def);
        f->onCommitted.connect([set = bound.set](const std::string& t) { set(JVariant(t)); });
        e.pull = [f, get = p.get] { f->setValue(get().toString()); };
        e.widget = std::move(field);
    } else {
        e = jMakePropertyEditor(m_graph, bound);
    }
    m_pulling = true;
    e.pull();
    m_pulling = false;
    m_pulls.push_back(e.pull);
    if (p.meta.editor == "long") {
        e.widget->setHSizePolicy(JSizePolicyMode::Expanding, 1);
    } else {
        // Never narrower than the control needs (a choice's arrow and its longest item).
        width = std::max(width, m_graph.getLayoutConst(e.widget->getNodeId()).minWidth);
        e.widget->setHSizePolicy(JSizePolicyMode::Fixed);
        e.widget->setFixedSize(width, p.get().isBool() ? st.checkHeight : st.controlHeight);
    }
    if (!p.meta.tooltip.empty()) e.widget->setTooltip(p.meta.tooltip);
    return std::move(e.widget);
}

std::unique_ptr<JWidget> JPSetupForm::group(const JPSetupProperties::Group& g, float& height, float labels) {
    const JStyle& st = JStyle::current();
    auto frame = std::make_unique<JPGroupFrame>(m_graph, g.title);
    frame->setAlignItems(JAlignItems::Stretch);

    // The labels' column is the page's (`labels`); each column under a header
    // is as wide as the widest thing in it.
    std::vector<float> columns;
    auto widen = [&columns](size_t i, float w) {
        if (columns.size() <= i) columns.resize(i + 1, 0.f);
        columns[i] = std::max(columns[i], w);
    };
    bool underHeader = false;
    for (const Row& r : g.rows) {
        if (r.kind == Row::Kind::Header) {
            // One without titles ends the columns.
            underHeader = !r.cells.empty();
            for (size_t i = 0; i < r.cells.size(); ++i) widen(i, JTextHelper::measureWidth(tr(r.cells[i].label)));
        }
        if (r.kind != Row::Kind::Fields || !underHeader || !inGrid(r)) continue;
        // The header names the columns: their own labels are not shown.
        for (size_t i = 0; i < r.cells.size(); ++i) {
            if (r.cells[i].button) continue;
            const JProperty* p = find(r.cells[i].property);
            widen(i, p ? widthOf(*p) : numberWidth());   // an empty place keeps a number's room
        }
    }

    const float rowH = rowHeight();
    float inner = 0;
    auto place = [&](std::unique_ptr<JWidget> w, float h) {
        inner += (inner > 0 ? st.spacing : 0) + h;
        frame->add(std::move(w));
    };
    underHeader = false;
    for (const Row& r : g.rows) {
        switch (r.kind) {
            case Row::Kind::Header: {
                underHeader = !r.cells.empty();
                if (!underHeader) break;
                auto row = JPUiParts::row(m_graph);
                row->add(box(m_graph, labels, st.labelHeight, JJustifyContent::FlexEnd));
                for (size_t i = 0; i < r.cells.size(); ++i) {
                    auto b = box(m_graph, columns[i], st.labelHeight, JJustifyContent::Center);
                    b->add(label(m_graph, r.cells[i].label));
                    row->add(std::move(b));
                }
                row->setFixedSize(0.f, st.labelHeight);
                row->setHSizePolicy(JSizePolicyMode::Expanding, 1);
                place(std::move(row), st.labelHeight);
                break;
            }
            case Row::Kind::Note: {
                // Folded to the width the group has (the form's, less the frame and scroll bar).
                const float formWidth = m_graph.getLayoutConst(getNodeId()).boundingBox.width;
                const float width = std::max(numberWidth() * 4, formWidth - JPGroupFrame::extraWidth()
                                                                     - 2 * (st.scrollBarWidth + st.itemPadding));
                auto note = std::make_unique<JLabel>(m_graph, r.text, width, st.labelHeight);
                note->setWordWrap(true);
                const float h = std::max(st.labelHeight, note->heightFor(width));
                note->setVSizePolicy(JSizePolicyMode::Fixed);
                note->setSize(width, h);
                place(std::move(note), h);
                break;
            }
            case Row::Kind::Plot: {
                // Its title, then the graph across the group's width.
                const float formWidth = m_graph.getLayoutConst(getNodeId()).boundingBox.width;
                const float width = std::max(numberWidth() * 4, formWidth - JPGroupFrame::extraWidth()
                                                                     - 2 * (st.scrollBarWidth + st.itemPadding));
                if (!r.label.empty()) place(label(m_graph, r.label), st.labelHeight);
                const float h = kPlotLines * st.labelHeight;
                auto view = std::make_unique<JPPlotView>(m_graph, r.plot);
                view->setFixedSize(width, h);
                place(std::move(view), h);
                break;
            }
            case Row::Kind::Strip: {
                // Across the group, a line high; nothing while it has no cells.
                const float formWidth = m_graph.getLayoutConst(getNodeId()).boundingBox.width;
                const float width = std::max(numberWidth() * 4, formWidth - JPGroupFrame::extraWidth()
                                                                     - 2 * (st.scrollBarWidth + st.itemPadding));
                const std::vector<int> now = r.strip ? r.strip() : std::vector<int> {};
                if (now.empty()) break;   // shown when the form is made again with cells
                auto strip = std::make_unique<JPSearchStrip>(m_graph);
                JPSearchStrip* shown = strip.get();
                shown->setStates(now);
                m_pulls.push_back([shown, get = r.strip] { shown->setStates(get ? get() : std::vector<int> {}); });
                strip->setFixedSize(width, st.labelHeight);
                place(std::move(strip), st.labelHeight);
                break;
            }
            case Row::Kind::Image: {
                // Its label in the labels' column, the picture beside it.
                float width = kImageLines * st.labelHeight, height = width;
                if (r.ownSize && r.image)
                    if (const auto shown = r.image(); shown && shown->width > 0) {
                        // As drawn, inside the box's border.
                        width = float(shown->width) + 2 * st.borderWidth;
                        height = float(shown->height) + 2 * st.borderWidth;
                    }
                auto row = JPUiParts::row(m_graph);
                auto name = box(m_graph, labels, height, JJustifyContent::FlexEnd);
                name->add(label(m_graph, r.label));
                row->add(std::move(name));
                JPImageBox* image = row->add(std::make_unique<JPImageBox>(m_graph, m_hal));
                image->setFixedSize(width, height);
                auto pull = [image, get = r.image] { image->setImage(get ? get() : nullptr); };
                pull();
                m_pulls.push_back(pull);
                row->setFixedSize(0.f, height);
                row->setHSizePolicy(JSizePolicyMode::Expanding, 1);
                place(std::move(row), height);
                break;
            }
            case Row::Kind::Actions: {
                auto row = JPUiParts::row(m_graph);
                row->add(box(m_graph, labels, rowH, JJustifyContent::FlexEnd));
                for (const JPSetupProperties::Cell& c : r.cells) row->add(button(c));
                place(std::move(row), rowH);
                break;
            }
            case Row::Kind::Fields: {
                // A row as tall as its tallest: a box of lines can be taller than a row.
                float h = rowH;
                for (const JPSetupProperties::Cell& c : r.cells)
                    if (const JProperty* p = find(c.property); p && p->meta.editor == "lines") h = std::max(h, linesHeight(*p));
                auto row = JPUiParts::row(m_graph);
                auto name = box(m_graph, labels, h, JJustifyContent::FlexEnd);
                JLabel* named = name->add(label(m_graph, r.label));
                if (!r.tooltip.empty()) named->setTooltip(r.tooltip);
                row->add(std::move(name));
                for (size_t i = 0; i < r.cells.size(); ++i) {
                    const JPSetupProperties::Cell& c = r.cells[i];
                    if (c.button && !c.icon.empty()) continue;   // after the place's buttons
                    if (c.button) {
                        row->add(button(c));
                        continue;
                    }
                    const JProperty* p = find(c.property);
                    if (underHeader && inGrid(r) && i < columns.size()) {
                        // In its column: the column's width, the control at its start
                        // (a box to tick under the middle of its title).
                        const bool tick = p && p->get().isBool();
                        const bool text = !p && !c.label.empty();   // a word in the column (OpenPnP's "1 ↔ 2")
                        auto cell = box(m_graph, columns[i], h,
                                        tick ? JJustifyContent::Center : text ? JJustifyContent::FlexEnd : JJustifyContent::FlexStart);
                        if (p) cell->add(editor(*p, widthOf(*p)));
                        else if (text) cell->add(label(m_graph, c.label));
                        row->add(std::move(cell));
                        continue;
                    }
                    if (!c.label.empty()) row->add(label(m_graph, c.label));
                    if (p) row->add(editor(*p, widthOf(*p)));
                }
                // A place: take it from where the camera or nozzle is, or go there.
                if (r.place == JPSetupProperties::Place::Location && m_openPnpPlaceButtons) {
                    locationButtons(*row, r);
                } else if (r.place == JPSetupProperties::Place::Location) {
                    struct B { const char* name; JPIconButton::Glyph glyph; const char* tip; Tool tool; bool capture; };
                    const B buttons[] = {
                        { "Capture Camera", &JPIcons::captureCamera, "Set from where the camera is", Tool::Camera, true },
                        { "Capture Nozzle", &JPIcons::captureNozzle, "Set from where the chosen nozzle is", Tool::Nozzle, true },
                        { "Move Camera", &JPIcons::moveCamera, "Move the camera here", Tool::Camera, false },
                        { "Move Nozzle", &JPIcons::moveNozzle, "Move the chosen nozzle here", Tool::Nozzle, false },
                    };
                    for (const B& b : buttons) {
                        JPIconButton* button = row->add(std::make_unique<JPIconButton>(m_graph, b.name, b.glyph, b.tip));
                        button->onClicked.connect([this, r, b] {
                            auto& handler = b.capture ? onCapture : onMoveTo;
                            if (handler) handler(r, b.tool);
                        });
                    }
                    optionalPlaceButtons(*row, r);
                }
                // Icon buttons last, as OpenPnP puts its own beside a place's.
                for (const JPSetupProperties::Cell& c : r.cells) {
                    if (!c.button || c.icon.empty()) continue;
                    JPIconButton* b = row->add(std::make_unique<JPIconButton>(m_graph, c.label, c.icon, c.tooltip));
                    b->setEnabled(c.enabled);
                    b->onClicked.connect([this, action = c.property] {
                        if (onAction) onAction(action);
                    });
                }
                if (r.place == JPSetupProperties::Place::Axis) {
                    JPIconButton* capture = row->add(std::make_unique<JPIconButton>(
                        m_graph, "Capture Axis", &JPIcons::captureCamera, "Set from where the axis is"));
                    capture->onClicked.connect([this, r] {
                        if (onCapture) onCapture(r, Tool::Camera);
                    });
                    JPIconButton* move = row->add(std::make_unique<JPIconButton>(
                        m_graph, "Move Axis", &JPIcons::moveCamera, "Move the axis here"));
                    move->onClicked.connect([this, r] {
                        if (onMoveTo) onMoveTo(r, Tool::Camera);
                    });
                }
                row->setFixedSize(0.f, h);
                row->setHSizePolicy(JSizePolicyMode::Expanding, 1);
                place(std::move(row), h);
                break;
            }
        }
    }
    height = inner + JPGroupFrame::extraHeight();
    frame->setVSizePolicy(JSizePolicyMode::Fixed);
    frame->setSize(0.f, height);
    return frame;
}

std::unique_ptr<JButton> JPSetupForm::button(const JPSetupProperties::Cell& c) {
    auto b = JPUiParts::button(m_graph, c.label);
    b->setEnabled(c.enabled);
    if (!c.tooltip.empty()) b->setTooltip(c.tooltip);
    b->onClicked.connect([this, action = c.property] {
        if (onAction) onAction(action);
    });
    return b;
}

void JPSetupForm::optionalPlaceButtons(JContainer& row, const Row& r) {
    const bool actuator = r.actuator && !r.actuator().empty();
    if (r.positionNoSafeZ) {
        JPIconButton* straight = row.add(std::make_unique<JPIconButton>(
            m_graph, actuator ? "Position Actuator (Without Safe Z)" : "Position Tool (Without Safe Z)",
            "position-nozzle-no-safe-z",
            actuator ? "Position the actuator over the center of the location without first moving to Safe Z."
                     : "Position the tool over the center of the location without first moving to Safe Z."));
        const Tool tool = actuator ? Tool::Actuator : Tool::Nozzle;
        straight->onClicked.connect([this, r, tool] {
            if (onMoveToStraight) onMoveToStraight(r, tool);
        });
    }
    if (r.contactProbe) {
        JPIconButton* probe = row.add(std::make_unique<JPIconButton>(
            m_graph, "Contact Probe Tool", "contact-probe-nozzle",
            "Position the tool over the center of the location then contact-probe Z."));
        probe->onClicked.connect([this, r] {
            if (onContactProbe) onContactProbe(r);
        });
    }
}

void JPSetupForm::locationButtons(JContainer& row, const Row& r) {
    // As OpenPnP's LocationButtonsPanel: go there with the camera or the
    // tool, then take it from where the camera or the tool is.
    // A row naming an actuator: its tool buttons are the actuator's.
    struct B { const char* name; const char* icon; const char* tip; Tool tool; bool capture; };
    const bool actuator = r.actuator && !r.actuator().empty();
    const B buttons[] = {
        { "Position Camera", "position-camera", "Position the camera over the center of the location.", Tool::Camera, false },
        actuator ? B { "Position Actuator", "position-actuator", "Position the actuator over the center of the location.",
                       Tool::Actuator, false }
                 : B { "Position Tool", "position-nozzle", "Position the tool over the center of the location.", Tool::Nozzle, false },
        { "Get Camera Coordinates", "capture-camera", "Capture the location that the camera is centered on.", Tool::Camera, true },
        actuator ? B { "Get Actuator Coordinates", "capture-actuator", "Capture the location that the actuator is centered on.",
                       Tool::Actuator, true }
                 : B { "Get Tool Coordinates", "capture-nozzle", "Capture the location that the tool is centered on.", Tool::Nozzle, true },
    };
    for (const B& b : buttons) {
        if (b.capture && b.tool == Tool::Camera) {
            // OpenPnP's optional ones, after Position Tool.
            optionalPlaceButtons(row, r);
            row.add(std::make_unique<JSeparator>(m_graph, JSeparator::JOrientation::Vertical, JPIconButton::size()));
        }
        JPIconButton* button = row.add(std::make_unique<JPIconButton>(m_graph, b.name, b.icon, b.tip));
        button->onClicked.connect([this, r, b] {
            auto& handler = b.capture ? onCapture : onMoveTo;
            if (handler) handler(r, b.tool);
        });
    }
}

} // inline namespace jf
