// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPlacerBoardUpgradeDialog.h"

#include "common/JPWhen.h"
#include "ui/JPUiParts.h"

#include <j/core/JStyle.h>

#include <algorithm>

inline namespace jf {

namespace {

// What a row is, in a word, the first that fits: the Change column.
std::string change(const JPBoardUpgrade::Row& r) {
    using K = JPBoardUpgrade::Kind;
    if (JPBoardUpgrade::is(r, K::New)) return r.toMatch ? "New (part to choose)" : "New";
    if (JPBoardUpgrade::is(r, K::Removed)) return "Removed";
    if (JPBoardUpgrade::is(r, K::FootprintChanged)) return "Footprint changed";
    if (JPBoardUpgrade::is(r, K::Moved)) return r.moved && r.turned ? "Moved and turned" : r.moved ? "Moved" : "Turned";
    if (JPBoardUpgrade::is(r, K::PartChanged)) return r.toMatch ? "Part changed (to choose)" : "Part changed";
    if (JPBoardUpgrade::is(r, K::Renamed)) return "Renamed";
    return "Unchanged";
}

} // namespace

JPlacerBoardUpgradeDialog::JPlacerBoardUpgradeDialog(JPBoard& board, std::shared_ptr<JPBoard> files,
                                                     std::function<void(JPBoardRevision, std::string)> made,
                                                     JGpuHal& hal, int sx, int sy, NativeWinHandleType parent)
    : JDialogWindow("New Revision of " + board.scopeName(), kW, kH, hal, sx, sy, parent)
    , m_board(board)
    , m_files(std::move(files))
    , m_upgrade(board, *m_files)
    , m_firstRevision(board.revisions().empty())
    , m_made(std::move(made)) {
    setResizable(true, kW * 2 / 3, kH * 2 / 3);
    JSceneGraph& g = graph();
    const JStyle& st = JStyle::current();
    m_content = std::make_unique<JContainer>(g, 0.f, 0.f);
    m_content->setDirection(JFlexDirection::Column)->setAlignItems(JAlignItems::Stretch)->setGap(st.spacing);
    auto note = [&](const std::string& text) {
        JLabel* l = m_content->add(std::make_unique<JLabel>(g, text, 0.f));
        l->setWordWrap(true);
        m_notes.push_back(l);
    };
    note(m_upgrade.summary() + ".");
    if (m_upgrade.originMoved())
        note("The new files' origin is elsewhere on the board: their positions are taken back by that move, so the "
             "board's place in a job and its fiducials still hold.");
    note("Decisions that still hold are kept: a placement's rotation correction, what a job set for it, its verified "
         "mark where nothing it was checked by changed, a part's choice where its line of the files is the same. The "
         "revision shown now is kept, to switch back to.");

    // The labels.
    auto labelRow = [&](const std::string& text, const std::string& value, const std::string& tip) {
        auto row = JPUiParts::row(g);
        JLabel* l = row->add(std::make_unique<JLabel>(g, text, 0.f));
        l->setFixedSize(JTextHelper::measureWidth("The revision shown now ") + st.spacing, st.controlHeight);
        JLineEdit* e = row->add(std::make_unique<JLineEdit>(g, ""));
        e->setText(value);
        e->setTooltip(tip);
        e->setHSizePolicy(JSizePolicyMode::Expanding, 1);
        add(e);
        m_content->add(std::move(row));
        return e;
    };
    const std::string current = m_firstRevision ? JPBoardRevision::next("") : board.revisionLabel();
    if (m_firstRevision)
        m_current = labelRow("The revision shown now", current,
                             "The board keeps no revisions yet: what it is now is kept as this one");
    std::string next = JPBoardRevision::next(current);
    while (board.revisionNamed(next) < board.revisions().size()) next = JPBoardRevision::next(next);
    m_label = labelRow("The new revision", next, "What the new revision is called (rev B, v2, the board's own name…)");
    m_label->onTextChanged.connect([this](const std::string&) { fill(); });
    if (m_current) m_current->onTextChanged.connect([this](const std::string&) { fill(); });

    // Show: all, or one count's list.
    auto showRow = JPUiParts::row(g);
    JLabel* showLabel = showRow->add(std::make_unique<JLabel>(g, "Show", 0.f));
    showLabel->setFixedSize(JTextHelper::measureWidth("The revision shown now ") + st.spacing, st.controlHeight);
    std::vector<std::string> shows { "All " + std::to_string(m_upgrade.rows().size()) + " placements" };
    for (JPBoardUpgrade::Kind k : JPBoardUpgrade::kKinds) shows.push_back(JPBoardUpgrade::words(k, m_upgrade.count(k)));
    m_show = showRow->add(std::make_unique<JComboBox>(g, shows));
    m_show->setCurrentIndex(0);
    m_show->setTooltip("Every placement, or only those of one kind of change");
    m_show->onIndexChanged.connect([this](int) { fill(); });
    add(m_show);
    m_content->add(std::move(showRow));

    m_list = m_content->add(std::make_unique<JDataGrid>(g, std::vector<std::string> { "Was", "Now", "Change", "What changed" }));
    m_list->setVSizePolicy(JSizePolicyMode::Expanding, 1);
    add(m_content.get());

    m_buttons = std::make_unique<JDialogButtonBox>(g);
    m_buttons->addButton("Cancel", JDialogButtonBox::Role::Reject)->setTooltip("Change nothing");
    m_make = m_buttons->addButton("Make the New Revision", JDialogButtonBox::Role::Accept);
    m_buttons->onReject.connect([this] { close(); });
    m_buttons->onAccept.connect([this] { make(); });
    add(m_buttons.get());
    fill();
}

void JPlacerBoardUpgradeDialog::fill() {
    const int show = m_show->currentIndex();
    std::vector<std::vector<std::string>> rows;
    for (const JPBoardUpgrade::Row& r : m_upgrade.rows())
        if (show <= 0 || JPBoardUpgrade::is(r, JPBoardUpgrade::kKinds[show - 1]))
            rows.push_back({ r.was, r.now, change(r), r.detail });
    m_list->setRows(rows);
    // Labels given, each its own.
    const std::string label = m_label->text();
    const bool free = m_board.revisionNamed(label) == m_board.revisions().size();
    m_make->setEnabled(!label.empty() && free && (!m_current || (!m_current->text().empty() && m_current->text() != label)));
    m_make->setTooltip(label.empty() ? "Give the new revision a label"
                       : !free || (m_current && m_current->text() == label)
                           ? "The board has a revision called " + label + " already"
                           : "Keep the revision shown now and show the new one; switch between them on the Boards tab");
}

void JPlacerBoardUpgradeDialog::make() {
    if (!m_make->isEnabled()) return;
    const std::string current = m_current ? m_current->text() : std::string();
    JPBoardRevision r = m_upgrade.revision(m_label->text(), JPWhen::now());
    close();
    if (m_made) m_made(std::move(r), current);
}

void JPlacerBoardUpgradeDialog::layout(float w, float h) {
    const JStyle& st = JStyle::current();
    const float pad = st.spacing * 2, cw = w - 2 * pad;
    const float buttonsY = h - pad - st.buttonHeight;
    const float top = contentTop();
    for (JLabel* n : m_notes) n->setMinimumSize(0.f, std::max(st.labelHeight, n->heightFor(cw)));
    m_content->setBounds({ pad, top, cw, std::max(0.f, buttonsY - st.spacing * 2 - top) });
    graph().invalidateNode(m_content->getNodeId(), DirtySelf);
    const JRect b = m_content->bounds();
    graph().computeLayout(m_content->getNodeId(), { b.width, b.width, b.height, b.height });
    m_buttons->setBounds({ pad, buttonsY, cw, st.buttonHeight });
    const float idW = JTextHelper::measureWidth("MMMMMMMM") + 2 * st.gridCellPadding;
    const float changeW = JTextHelper::measureWidth("Part changed (to choose)") + 2 * st.gridCellPadding;
    m_list->setColumnWidths({ idW, idW, changeW, std::max(0.f, cw - 2 * idW - changeW - st.scrollBarWidth) });
}

} // inline namespace jf
