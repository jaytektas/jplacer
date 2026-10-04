// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPFieldGrid.h"

#include <j/core/JStyle.h>
#include <j/core/JTextHelper.h>

#include <algorithm>

inline namespace jf {

JPFieldGrid::JPFieldGrid(JSceneGraph& graph, int pairsAcross)
    : JContainer(graph, 0.f, 0.f), m_pairs(std::max(1, pairsAcross)) {
    const JStyle& st = JStyle::current();
    setDirection(JFlexDirection::JRow)->setGap(2 * st.spacing)->setAlignItems(JAlignItems::Start);
    // A form for each column of pairs: its labels as wide as the widest, its
    // fields the rest; the columns share the width evenly.
    for (int i = 0; i < m_pairs; ++i) {
        auto form = std::make_unique<JContainer>(graph, 0.f, 0.f);
        form->setLayoutMode(JLayoutMode::Form)->setGap(st.spacing);
        form->setHSizePolicy(JSizePolicyMode::Expanding, 1);
        m_columns.push_back(add(std::move(form)));
    }
}

void JPFieldGrid::place(const std::string& text, const std::string& tooltip, std::unique_ptr<JWidget> field) {
    const JStyle& st = JStyle::current();
    JContainer* form = m_columns[size_t(m_placed % m_pairs)];
    ++m_placed;
    JLabel* l = form->add(std::make_unique<JLabel>(m_graph, text, 0.f));
    if (!tooltip.empty()) l->setTooltip(tooltip);
    // Against its field, as OpenPnP's forms set a label ("right, default").
    m_graph.getLayout(l->getNodeId()).cellAlign = JCellAlign::End;
    l->setSize(text.empty() ? 0.f : JTextHelper::measureWidth(text) + st.spacing, st.controlHeight);
    field->setSize(0.f, st.controlHeight);
    form->add(std::move(field));
}

JPTextField* JPFieldGrid::text(const std::string& name, const std::string& tooltip, const std::string& value,
                               std::function<bool(const std::string&)> commit) {
    auto f = std::make_unique<JPTextField>(m_graph);
    JPTextField* raw = f.get();
    raw->setValue(value);
    raw->onCommitted.connect([raw, commit, value](std::string t) {
        // A value refused shows what was there; one taken stays as typed.
        if (!commit(t)) raw->setValue(value);
    });
    place(name, tooltip, std::move(f));
    return raw;
}

JComboBox* JPFieldGrid::choice(const std::string& name, const std::string& tooltip, const std::vector<std::string>& items,
                               int current, std::function<void(int)> chosen) {
    auto c = std::make_unique<JComboBox>(m_graph, items);
    JComboBox* raw = c.get();
    raw->setCurrentIndex(current);
    raw->onIndexChanged.connect([chosen](int i) { if (i >= 0) chosen(i); });
    place(name, tooltip, std::move(c));
    return raw;
}

JCheckBox* JPFieldGrid::tick(const std::string& name, const std::string& tooltip, bool on, std::function<void(bool)> changed) {
    auto b = std::make_unique<JCheckBox>(m_graph, "", 0.f);
    JCheckBox* raw = b.get();
    raw->setChecked(on);
    raw->onStateChanged.connect([changed](bool v) { changed(v); });
    place(name, tooltip, std::move(b));
    return raw;
}

void JPFieldGrid::skip() {
    auto empty = std::make_unique<JContainer>(m_graph, 0.f, 0.f);
    place("", "", std::move(empty));
}

std::unique_ptr<JPGroupFrame> JPFieldGrid::grouped(JSceneGraph& g, const std::string& title, std::unique_ptr<JPFieldGrid> grid) {
    auto frame = std::make_unique<JPGroupFrame>(g, title);
    frame->setAlignItems(JAlignItems::Stretch);
    const float h = grid->rowsHeight();
    grid->setVSizePolicy(JSizePolicyMode::Fixed);
    grid->setSize(0.f, h);
    frame->add(std::move(grid));
    frame->setVSizePolicy(JSizePolicyMode::Fixed);
    frame->setSize(0.f, h + JPGroupFrame::extraHeight());
    return frame;
}

float JPFieldGrid::rowsHeight() const {
    const JStyle& st = JStyle::current();
    const int rows = (m_placed + m_pairs - 1) / m_pairs;
    return float(rows) * st.controlHeight + float(std::max(0, rows - 1)) * st.spacing;
}

} // inline namespace jf
