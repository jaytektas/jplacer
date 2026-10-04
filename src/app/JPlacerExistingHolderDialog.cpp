// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPlacerExistingHolderDialog.h"

#include <j/core/Dialog.h>
#include <j/core/JButton.h>
#include <j/core/JStyle.h>

#include <algorithm>

inline namespace jf {

JPlacerExistingHolderDialog::JPlacerExistingHolderDialog(const std::string& title, const std::string& what,
                                                         std::vector<std::string> files,
                                                         std::function<void(std::string)> onChosen, JGpuHal& hal,
                                                         int sx, int sy, NativeWinHandleType parent)
    : JDialogWindow(title, kW, kH, hal, sx, sy, parent), m_files(std::move(files)), m_onChosen(std::move(onChosen)) {
    setResizable(true, kW / 2, kH / 2);
    m_prompt = std::make_unique<JLabel>(graph(), "Select a " + what +
                                                     " from the list below or click Browse to locate one in the file system",
                                        0.f);
    add(m_prompt.get());
    m_list = std::make_unique<JListView>(graph(), m_files);
    m_list->onSelectionChanged.connect([this](int i) { m_ok->setEnabled(i >= 0); });
    m_list->onItemActivated.connect([this](int i) {
        if (i < 0 || size_t(i) >= m_files.size()) return;
        close();
        if (m_onChosen) m_onChosen(m_files[size_t(i)]);
    });
    add(m_list.get());
    m_buttons = std::make_unique<JDialogButtonBox>(graph());
    m_ok = m_buttons->addButton("OK", JDialogButtonBox::Role::Accept);
    m_ok->setEnabled(false);
    m_buttons->addButton("Browse", JDialogButtonBox::Role::Action)->onClicked.connect([this, title] {
        close();
        JDialog::openFile(title, { "xml" }, [onChosen = m_onChosen](std::string path) {
            if (onChosen) onChosen(path);
        });
    });
    m_buttons->addButton("Cancel", JDialogButtonBox::Role::Reject);
    m_buttons->onAccept.connect([this] {
        const int i = m_list->selectedIndex();
        if (i < 0 || size_t(i) >= m_files.size()) return;
        close();
        if (m_onChosen) m_onChosen(m_files[size_t(i)]);
    });
    m_buttons->onReject.connect([this] { close(); });
    add(m_buttons.get());
}

void JPlacerExistingHolderDialog::layout(float w, float h) {
    const JStyle& st = JStyle::current();
    const float pad = st.spacing * 2, cw = w - 2 * pad;
    const float buttonsY = h - pad - st.buttonHeight;
    float y = contentTop();
    m_prompt->setBounds({ pad, y, cw, st.labelHeight });
    y += st.labelHeight + st.spacing;
    m_list->setBounds({ pad, y, cw, std::max(0.f, buttonsY - st.spacing * 2 - y) });
    m_buttons->setBounds({ pad, buttonsY, cw, st.buttonHeight });
}

} // inline namespace jf
