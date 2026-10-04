// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPFormPage.h"

#include "JPUiParts.h"

#include <j/core/JScrollArea.h>
#include <j/core/JStyle.h>
#include <j/core/JTextHelper.h>

#include <algorithm>
#include <cmath>

inline namespace jf {

JPFormPage::JPFormPage(JSceneGraph& graph, const std::vector<Field>& fields, const std::vector<Action>& actions,
                       std::unique_ptr<JWidget> picture)
    : JContainer(graph) {
    JPUiParts::asPanel(*this);
    const JStyle& st = JStyle::current();
    m_note = add(std::make_unique<JLabel>(graph, ""));
    m_note->setWordWrap(true);

    // The fields, scrolled when the dock is short, the picture beside them.
    auto body = std::make_unique<JContainer>(graph);
    body->setDirection(JFlexDirection::JRow)->setGap(2 * st.spacing)->setAlignItems(JAlignItems::Stretch);
    body->setVSizePolicy(JSizePolicyMode::Expanding, 1);
    auto* scroll = body->add(std::make_unique<JScrollArea>(graph));
    scroll->setHSizePolicy(JSizePolicyMode::Expanding, 1);
    float widest = 0;
    for (const Field& f : fields)
        widest = std::max(widest, JTextHelper::hasAtlas() ? JTextHelper::measureWidth(f.label) : 0.f);
    for (const Field& f : fields) {
        JContainer* r = scroll->addChildWidget(JPUiParts::row(graph));
        auto label = std::make_unique<JLabel>(graph, f.label, 0.f);
        label->setFixedSize(std::ceil(widest) + 2 * st.spacing, st.labelHeight);
        r->add(std::move(label));
        const std::string key = f.key;
        if (f.choice) {
            JComboBox* c = r->add(std::make_unique<JComboBox>(graph, std::vector<std::string>{}));
            c->setHSizePolicy(JSizePolicyMode::Expanding, 1);
            c->onIndexChanged.connect([this, key](int i) {
                if (!m_updating && onChosen && i >= 0) onChosen(key, i);
            });
            m_choices[key] = c;
        } else {
            JPTextField* t = r->add(std::make_unique<JPTextField>(graph));
            t->setHSizePolicy(JSizePolicyMode::Expanding, 1);
            t->onCommitted.connect([this, key](std::string v) {
                if (!m_updating && onField) onField(key, v);
            });
            m_texts[key] = t;
        }
    }
    if (picture) {
        picture->setHSizePolicy(JSizePolicyMode::Expanding, 1);
        m_picture = body->add(std::move(picture));
    }
    add(std::move(body));

    if (!actions.empty()) {
        auto buttons = JPUiParts::row(graph);
        for (const Action& a : actions) {
            JButton* b = buttons->add(JPUiParts::button(graph, a.label));
            const std::string key = a.key;
            b->onClicked.connect([this, key] {
                if (onAction) onAction(key);
            });
            m_buttons[key] = b;
        }
        add(std::move(buttons));
    }
}

void JPFormPage::setNote(const std::string& text) {
    m_note->setText(text);
}

void JPFormPage::setValue(const std::string& key, const std::string& text) {
    if (const auto it = m_texts.find(key); it != m_texts.end()) it->second->setValue(text);
}

void JPFormPage::setChoices(const std::string& key, const std::vector<std::string>& items, int chosen) {
    const auto it = m_choices.find(key);
    if (it == m_choices.end()) return;
    m_updating = true;
    it->second->setItems(items);
    it->second->setCurrentIndex(chosen);
    m_updating = false;
}

void JPFormPage::setEditable(bool editable) {
    for (auto& [key, t] : m_texts) t->setEnabled(editable);
    for (auto& [key, c] : m_choices) c->setEnabled(editable);
}

void JPFormPage::setFieldEditable(const std::string& key, bool editable) {
    if (const auto it = m_texts.find(key); it != m_texts.end()) it->second->setEnabled(editable);
    if (const auto it = m_choices.find(key); it != m_choices.end()) it->second->setEnabled(editable);
}

void JPFormPage::setActionEnabled(const std::string& key, bool enabled) {
    if (const auto it = m_buttons.find(key); it != m_buttons.end()) it->second->setEnabled(enabled);
}

} // inline namespace jf
