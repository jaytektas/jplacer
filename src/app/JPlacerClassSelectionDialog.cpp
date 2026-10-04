// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPlacerClassSelectionDialog.h"

#include <j/core/JButton.h>
#include <j/core/JStyle.h>

#include <algorithm>

inline namespace jf {

namespace {

std::vector<std::string> simpleNames(const std::vector<std::string>& classes) {
    std::vector<std::string> out;
    for (const std::string& c : classes) {
        const size_t dot = c.rfind('.');
        out.push_back(dot == std::string::npos ? c : c.substr(dot + 1));
    }
    return out;
}

} // namespace

JPlacerClassSelectionDialog::JPlacerClassSelectionDialog(const std::string& title, const std::string& description,
                                                         std::vector<std::string> classes,
                                                         std::function<void(std::string)> onChosen, JGpuHal& hal,
                                                         int sx, int sy, NativeWinHandleType parent)
    : JDialogWindow(title, kW, kH, hal, sx, sy, parent), m_classes(std::move(classes)), m_onChosen(std::move(onChosen)) {
    setResizable(true, kW / 2, kH / 2);
    m_description = std::make_unique<JLabel>(graph(), description, 0.f);
    add(m_description.get());
    m_list = std::make_unique<JListView>(graph(), simpleNames(m_classes));
    m_list->onSelectionChanged.connect([this](int i) { m_accept->setEnabled(i >= 0); });
    // A single click chooses; a double-click accepts, as OpenPnP's list does.
    m_list->onItemActivated.connect([this](int i) {
        if (m_clicks.click(i)) accept(i);
    });
    add(m_list.get());
    m_buttons = std::make_unique<JDialogButtonBox>(graph());
    m_buttons->addButton("Cancel", JDialogButtonBox::Role::Reject);
    m_accept = m_buttons->addButton("Accept", JDialogButtonBox::Role::Accept);
    m_accept->setEnabled(false);
    m_buttons->onAccept.connect([this] { accept(m_list->selectedIndex()); });
    m_buttons->onReject.connect([this] { close(); });
    add(m_buttons.get());
}

void JPlacerClassSelectionDialog::accept(int index) {
    if (index < 0 || size_t(index) >= m_classes.size()) return;
    close();
    if (m_onChosen) m_onChosen(m_classes[size_t(index)]);
}

void JPlacerClassSelectionDialog::layout(float w, float h) {
    const JStyle& st = JStyle::current();
    const float pad = st.spacing * 2, cw = w - 2 * pad;
    const float buttonsY = h - pad - st.buttonHeight;
    float y = contentTop();
    m_description->setBounds({ pad, y, cw, st.labelHeight });
    y += st.labelHeight + st.spacing;
    m_list->setBounds({ pad, y, cw, std::max(0.f, buttonsY - st.spacing * 2 - y) });
    m_buttons->setBounds({ pad, buttonsY, cw, st.buttonHeight });
}

} // inline namespace jf
