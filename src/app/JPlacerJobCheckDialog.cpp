// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPlacerJobCheckDialog.h"

#include <j/core/JStyle.h>
#include <j/core/JTextHelper.h>

#include <algorithm>

inline namespace jf {

JPlacerJobCheckDialog::JPlacerJobCheckDialog(std::vector<JPJobCheck::Item> items, std::function<void()> start, JGpuHal& hal,
                                             int sx, int sy, NativeWinHandleType parent)
    : JDialogWindow("Check Job", kW, kH, hal, sx, sy, parent) {
    setResizable(true, kW / 2, kH / 2);
    JSceneGraph& g = graph();
    const JStyle& st = JStyle::current();
    m_content = std::make_unique<JContainer>(g, 0.f, 0.f);
    m_content->setDirection(JFlexDirection::Column)->setAlignItems(JAlignItems::Stretch)->setGap(st.spacing);
    m_summary = m_content->add(std::make_unique<JLabel>(g, "", 0.f));
    m_summary->setWordWrap(true);
    m_list = m_content->add(std::make_unique<JDataGrid>(g, std::vector<std::string> { "", "What", "About", "Put right in" }));
    m_list->setVSizePolicy(JSizePolicyMode::Expanding, 1);
    m_detail = m_content->add(std::make_unique<JLabel>(g, "", 0.f));
    m_detail->setWordWrap(true);
    m_list->onSelectionChanged.connect([this](int row) {
        if (row < 0 || size_t(row) >= m_items.size()) {
            m_detail->setText("Choose a row to see all it is about.");
            return;
        }
        const JPJobCheck::Item& i = m_items[size_t(row)];
        std::string all;
        for (size_t k = 0; k < i.which.size() && k < kDetailNamed; ++k) all += (k ? ", " : "") + i.which[k];
        if (i.which.size() > kDetailNamed) all += " and " + std::to_string(i.which.size() - kDetailNamed) + " more";
        m_detail->setText(i.what + ": " + all + ". Put right in " + i.fix + ".");
    });
    add(m_content.get());

    size_t stops = 0, checks = 0, notes = 0;
    std::vector<std::vector<std::string>> rows;
    for (const JPJobCheck::Item& i : items) {
        (i.level == JPJobCheck::Item::Level::Stop ? stops : i.level == JPJobCheck::Item::Level::Check ? checks : notes)++;
        std::string about;
        for (size_t k = 0; k < i.which.size() && k < kNamed; ++k) about += (k ? ", " : "") + i.which[k];
        if (i.which.size() > kNamed) about += " and " + std::to_string(i.which.size() - kNamed) + " more";
        rows.push_back({ JPJobCheck::levelName(i.level), i.what + " (" + std::to_string(i.which.size()) + ")", about, i.fix });
    }
    m_list->setRows(rows);
    m_items = std::move(items);
    m_detail->setText(m_items.empty() ? std::string() : "Choose a row to see all it is about.");
    const bool fromStart = bool(start);
    std::string summary = m_items.empty() ? "Nothing to put right: the job is ready to run."
                                        : std::to_string(stops) + " to put right before it runs, " + std::to_string(checks) +
                                              " to check, " + std::to_string(notes) + " to note.";
    if (fromStart && stops > 0) summary += " The run would stop at the first of them, so it is not started.";
    else if (fromStart && !m_items.empty()) summary += " Start Anyway runs it as it is.";
    m_summary->setText(summary);

    m_buttons = std::make_unique<JDialogButtonBox>(g);
    if (fromStart && stops == 0) {
        m_buttons->addButton("Cancel", JDialogButtonBox::Role::Reject)->setTooltip("Do not start; put things right first");
        JButton* go = m_buttons->addButton("Start Anyway", JDialogButtonBox::Role::Accept);
        go->setTooltip("Run the job as it is");
        m_buttons->onAccept.connect([this, start] {
            close();
            start();
        });
    } else {
        m_buttons->addButton("Close", JDialogButtonBox::Role::Accept);
        m_buttons->onAccept.connect([this] { close(); });
    }
    m_buttons->onReject.connect([this] { close(); });
    add(m_buttons.get());
}

void JPlacerJobCheckDialog::layout(float w, float h) {
    const JStyle& st = JStyle::current();
    const float pad = st.spacing * 2, cw = w - 2 * pad;
    const float buttonsY = h - pad - st.buttonHeight;
    const float top = contentTop();
    m_summary->setMinimumSize(0.f, std::max(st.labelHeight, m_summary->heightFor(cw)));
    m_detail->setMinimumSize(0.f, std::max(st.labelHeight, m_detail->heightFor(cw)));
    m_content->setBounds({ pad, top, cw, std::max(0.f, buttonsY - st.spacing * 2 - top) });
    graph().invalidateNode(m_content->getNodeId(), DirtySelf);
    const JRect b = m_content->bounds();
    graph().computeLayout(m_content->getNodeId(), { b.width, b.width, b.height, b.height });
    m_buttons->setBounds({ pad, buttonsY, cw, st.buttonHeight });
    const float level = JTextHelper::measureWidth("Check") + 4 * st.gridCellPadding;
    const float rest = std::max(0.f, cw - level - st.scrollBarWidth);
    m_list->setColumnWidths({ level, rest * 0.3f, rest * 0.4f, rest * 0.3f });
}

} // inline namespace jf
