// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPSetupForm.h"

#include "JPGroupFrame.h"
#include "JPIconButton.h"
#include "JPIcons.h"
#include "JPTextBox.h"
#include "JPTextField.h"
#include "JPUiParts.h"

#include <j/core/JButton.h>
#include <j/core/JLabel.h>
#include <j/core/JPropertyBinding.h>
#include <j/core/JScrollArea.h>
#include <j/core/JStyle.h>
#include <j/core/JTextHelper.h>

#include <algorithm>
#include <cmath>

inline namespace jf {

namespace {

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
    l->setFixedSize(std::ceil(JTextHelper::measureWidth(text)) + st.spacing, st.labelHeight);
    return l;
}

} // namespace

JPSetupForm::JPSetupForm(JSceneGraph& graph) : JContainer(graph, 0.f, 0.f) {
    setDirection(JFlexDirection::Column)->setAlignItems(JAlignItems::Stretch);
    m_tabs = add(std::make_unique<JTabWidget>(graph, 0.f, 0.f));
    m_tabs->setVSizePolicy(JSizePolicyMode::Expanding, 1);
}

void JPSetupForm::setForm(JPSetupProperties::Form form) {
    // The same tab as before, when this part has one of that name.
    const int was = m_tabs->activeTab();
    const std::string wasTitle = was >= 0 && was < int(m_form.tabs.size()) ? m_form.tabs[size_t(was)].title : "";
    while (m_tabs->tabCount() > 0) m_tabs->removeTab(m_tabs->tabCount() - 1);
    m_pages.clear();
    m_pulls.clear();
    m_form = std::move(form);
    int active = 0;
    for (size_t i = 0; i < m_form.tabs.size(); ++i) {
        m_pages.push_back(page(m_form.tabs[i]));
        m_tabs->addTab(m_form.tabs[i].title, m_pages.back().get());
        if (m_form.tabs[i].title == wasTitle) active = int(i);
    }
    if (!m_pages.empty()) m_tabs->setActiveTab(active);
    invalidate();
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
    float height = 0;
    for (const JPSetupProperties::Group& g : tab.groups) {
        float h = 0;
        content->add(group(g, h));
        height += (height > 0 ? 2 * st.spacing : 0) + h;
    }
    // As tall as its groups: it sits in a scroll area, which goes by its children's heights.
    content->setVSizePolicy(JSizePolicyMode::Fixed);
    content->setSize(m_graph.getLayoutConst(content->getNodeId()).boundingBox.width, height + st.spacing);
    scroll->addChildWidget(std::move(content));
    return scroll;
}

float JPSetupForm::widthOf(const JProperty& p) const {
    const JStyle& st = JStyle::current();
    if (!p.writable()) return JTextHelper::measureWidth(p.get().toString());
    if (!p.meta.choices.empty()) {
        // The longest item, its padding either side, and the arrow.
        float widest = 0;
        for (const JVariant& c : p.meta.choices) widest = std::max(widest, JTextHelper::measureWidth(c.toString()));
        return std::ceil(widest) + 2 * st.fieldPadding + st.controlHeight + 2 * st.spacing;
    }
    const JVariant v = p.get();
    if (v.isBool()) return st.checkHeight;
    if (v.isInt() || v.isDouble() || p.meta.editor == "number") return numberWidth();
    return 2 * numberWidth();   // a name, a line of text ("long" ones take the row's room)
}

std::unique_ptr<JWidget> JPSetupForm::editor(const JProperty& p, float width) {
    const JStyle& st = JStyle::current();
    if (!p.writable()) {
        auto value = std::make_unique<JLabel>(m_graph, p.get().toString(), 0.f, st.labelHeight);
        JLabel* v = value.get();
        v->setMinWidthFollowsText(true);
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
    if (isText(p)) {
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

std::unique_ptr<JWidget> JPSetupForm::group(const JPSetupProperties::Group& g, float& height) {
    const JStyle& st = JStyle::current();
    auto frame = std::make_unique<JPGroupFrame>(m_graph, g.title);
    frame->setAlignItems(JAlignItems::Stretch);

    // The labels' column, as wide as the widest; and the width of each column
    // under a header, as wide as the widest thing in it.
    float labels = 0;
    for (const Row& r : g.rows)
        if (r.kind == Row::Kind::Fields) labels = std::max(labels, std::ceil(JTextHelper::measureWidth(r.label)) + st.spacing);
    std::vector<float> columns;
    auto widen = [&columns](size_t i, float w) {
        if (columns.size() <= i) columns.resize(i + 1, 0.f);
        columns[i] = std::max(columns[i], w);
    };
    bool underHeader = false;
    for (const Row& r : g.rows) {
        if (r.kind == Row::Kind::Header) {
            underHeader = true;
            for (size_t i = 0; i < r.cells.size(); ++i) widen(i, JTextHelper::measureWidth(r.cells[i].label));
        }
        if (r.kind != Row::Kind::Fields || !underHeader || !inGrid(r)) continue;
        // The header names the columns: their own labels are not shown.
        for (size_t i = 0; i < r.cells.size(); ++i) {
            const JProperty* p = find(r.cells[i].property);
            widen(i, p ? widthOf(*p) : 0.f);
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
                underHeader = true;
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
                auto note = std::make_unique<JLabel>(m_graph, r.text, 0.f, st.labelHeight);
                place(std::move(note), st.labelHeight);
                break;
            }
            case Row::Kind::Actions: {
                auto row = JPUiParts::row(m_graph);
                row->add(box(m_graph, labels, rowH, JJustifyContent::FlexEnd));
                for (const JPSetupProperties::Cell& c : r.cells) {
                    JButton* b = row->add(JPUiParts::button(m_graph, c.label));
                    b->onClicked.connect([this, action = c.property] {
                        if (onAction) onAction(action);
                    });
                }
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
                name->add(label(m_graph, r.label));
                row->add(std::move(name));
                for (size_t i = 0; i < r.cells.size(); ++i) {
                    const JPSetupProperties::Cell& c = r.cells[i];
                    const JProperty* p = find(c.property);
                    if (underHeader && inGrid(r) && i < columns.size()) {
                        // In its column: the column's width, the control at its start
                        // (a box to tick under the middle of its title).
                        const bool tick = p && p->get().isBool();
                        auto cell = box(m_graph, columns[i], h, tick ? JJustifyContent::Center : JJustifyContent::FlexStart);
                        if (p) cell->add(editor(*p, widthOf(*p)));
                        row->add(std::move(cell));
                        continue;
                    }
                    if (!c.label.empty()) row->add(label(m_graph, c.label));
                    if (p) row->add(editor(*p, widthOf(*p)));
                }
                // A place: take it from where the camera or nozzle is, or go there.
                if (r.place == JPSetupProperties::Place::Location) {
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

} // inline namespace jf
