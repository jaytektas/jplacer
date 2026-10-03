// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPPropertyForm.h"

#include "JPTextField.h"
#include "JPUiParts.h"


#include <j/core/JLabel.h>
#include <j/core/JPropertyBinding.h>
#include <j/core/JStyle.h>
#include <j/core/JTextHelper.h>

#include <algorithm>

inline namespace jf {

JPPropertyForm::JPPropertyForm(JSceneGraph& graph) : JContainer(graph) {
    const JStyle& st = JStyle::current();
    setDirection(JFlexDirection::Column)->setGap(2 * st.spacing)->setAlignItems(JAlignItems::Stretch);
    setVSizePolicy(JSizePolicyMode::Fixed);
}

void JPPropertyForm::setModel(JPropertyModel model) {
    clear();
    m_pulls.clear();
    m_model = std::move(model);
    const JStyle& st = JStyle::current();
    // The labels in a column as wide as the widest; the controls take the rest.
    float labelWidth = 0;
    for (const JProperty& p : m_model.all())
        labelWidth = std::max(labelWidth, JTextHelper::measureWidth(p.meta.label.empty() ? p.name : p.meta.label));
    labelWidth += 2 * st.spacing;
    const float gap = 2 * st.spacing;
    float height = 0;
    auto place = [&](std::unique_ptr<JWidget> w, float h) {
        height += (height > 0 ? gap : 0) + h;
        add(std::move(w));
    };
    std::string category;
    bool first = true;
    for (const JProperty& p : m_model.all()) {
        if (first || p.meta.category != category) {
            first = false;
            category = p.meta.category;
            if (!category.empty()) place(std::make_unique<JLabel>(m_graph, category), st.labelHeight);
        }
        auto row = JPUiParts::row(m_graph);
        const float rowHeight = std::max(st.buttonHeight, st.controlHeight);
        JLabel* label = row->add(std::make_unique<JLabel>(m_graph, p.meta.label.empty() ? p.name : p.meta.label));
        label->setFixedSize(labelWidth, st.labelHeight);
        label->setHSizePolicy(JSizePolicyMode::Fixed);
        if (!p.writable()) {
            JLabel* value = row->add(std::make_unique<JLabel>(m_graph, p.get().toString()));
            value->setHSizePolicy(JSizePolicyMode::Expanding, 1);
            m_pulls.push_back([value, get = p.get] { value->setText(get().toString()); });
            place(std::move(row), rowHeight);
            continue;
        }
        // Each edit goes through the setter, then is reported; a control set
        // by refresh() is not an edit.
        JProperty bound = p;
        bound.set = [this, set = p.set, name = p.name](const JVariant& v) {
            if (m_pulling) return true;
            const bool ok = set(v);
            if (ok && onChanged) onChanged(name);
            return ok;
        };
        // A line of text changes when it is committed (Return, Tab, leaving
        // it), as a number does; the framework's editor for it changes on each key.
        const JVariant now = p.get();
        const bool text = p.meta.choices.empty() && !now.isBool() && !now.isInt() && !now.isDouble();
        JPropertyEditor e;
        if (text) {
            auto field = std::make_unique<JPTextField>(m_graph);
            JPTextField* f = field.get();
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
        e.widget->setHSizePolicy(JSizePolicyMode::Expanding, 1);
        row->add(std::move(e.widget));
        place(std::move(row), rowHeight);
    }
    // As tall as its rows: it sits in a scroll area, which goes by its children's heights.
    setSize(m_graph.getLayoutConst(getNodeId()).boundingBox.width, height);
}

void JPPropertyForm::refresh() {
    m_pulling = true;
    for (const auto& pull : m_pulls) pull();
    m_pulling = false;
}

} // inline namespace jf
