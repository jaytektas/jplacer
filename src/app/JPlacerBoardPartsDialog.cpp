// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPlacerBoardPartsDialog.h"

#include "model/JPPartMatcher.h"

#include <j/core/JStyle.h>

#include <algorithm>

inline namespace jf {

namespace {

constexpr size_t kDesignatorsShown = 6;

std::string designators(const std::vector<std::string>& ids) {
    std::string s;
    for (size_t i = 0; i < ids.size() && i < kDesignatorsShown; ++i) s += (i ? ", " : "") + ids[i];
    if (ids.size() > kDesignatorsShown) s += " … (" + std::to_string(ids.size()) + ")";
    return s;
}

std::string now(const JPBoardPart& bp) {
    switch (bp.state) {
        case JPBoardPart::State::Matched: return bp.libraryPartId;
        case JPBoardPart::State::Local:   return "the board's own: " + bp.partId();
        case JPBoardPart::State::Unmatched: break;
    }
    return "to be chosen";
}

} // namespace

JPlacerBoardPartsDialog::JPlacerBoardPartsDialog(const JPConfiguration& config, const JPBoard& board, Apply apply, Pick pick,
                                                 JGpuHal& hal, int sx, int sy, NativeWinHandleType parent)
    : JDialogWindow("Parts of " + board.scopeName(), kW, kH, hal, sx, sy, parent)
    , m_config(config)
    , m_board(board)
    , m_apply(std::move(apply))
    , m_pick(std::move(pick)) {
    setResizable(true, kW * 2 / 3, kH * 2 / 3);
    JSceneGraph& g = graph();
    const JStyle& st = JStyle::current();
    m_content = std::make_unique<JContainer>(g, 0.f, 0.f);
    m_content->setDirection(JFlexDirection::Column)->setAlignItems(JAlignItems::Stretch)->setGap(st.spacing);
    m_summary = m_content->add(std::make_unique<JLabel>(g, "", 0.f));
    m_list = m_content->add(std::make_unique<JDataGrid>(
        g, std::vector<std::string>{ "Placements", "Value", "Footprint", "MPN", "Is", "Best match", "Why" }));
    m_list->setVSizePolicy(JSizePolicyMode::Expanding, 1);
    m_list->onSelectionChanged.connect([this](int i) { m_choose->setEnabled(i >= 0); });
    m_list->onRowActivated.connect([this](int i) { m_activated = i; });
    m_onlyToChoose = m_content->add(std::make_unique<JCheckBox>(g, "Only those to be chosen", 0.f));
    m_onlyToChoose->setTooltip("List only the parts not yet the library's or the board's own");
    m_onlyToChoose->onStateChanged.connect([this](bool) { m_refill = true; });
    add(m_content.get());

    m_buttons = std::make_unique<JDialogButtonBox>(g);
    m_best = m_buttons->addButton("Use Best Matches", JDialogButtonBox::Role::Action);
    m_best->setTooltip("Each part still to be chosen takes its best match, where the evidence is strong (not the "
                       "value alone)");
    m_best->onClicked.connect([this] { useBest(); });
    m_choose = m_buttons->addButton("Choose…", JDialogButtonBox::Role::Action);
    m_choose->setTooltip("Choose the part chosen in the list, in the part picker");
    m_choose->onClicked.connect([this] { chooseSelected(); });
    m_buttons->addButton("Close", JDialogButtonBox::Role::Reject);
    m_buttons->onReject.connect([this] { close(); });
    add(m_buttons.get());
    fill();
}

void JPlacerBoardPartsDialog::fill() {
    const int keep = m_list->selectedIndex();
    const std::string keepKey = keep >= 0 && size_t(keep) < m_rowKeys.size() ? m_rowKeys[size_t(keep)] : "";
    m_rowKeys.clear();
    std::vector<std::vector<std::string>> rows;
    int toChoose = 0, strong = 0;
    for (const JPBoardPart& bp : m_board.parts()) {
        const std::vector<std::string> ids = m_board.placementsOf(bp.key);
        if (ids.empty()) continue;
        const bool open = bp.state == JPBoardPart::State::Unmatched;
        const auto c = JPPartMatcher::candidates(m_config, bp);
        if (open) {
            ++toChoose;
            if (!c.empty() && c.front().score >= JPPartMatcher::kStrong) ++strong;
        }
        if (m_onlyToChoose->isChecked() && !open) continue;
        const bool sameAsNow = bp.state == JPBoardPart::State::Matched && !c.empty() && c.front().part->id == bp.libraryPartId;
        const std::string footprint = !bp.field("footprint").empty() ? bp.field("footprint") : bp.field("package");
        rows.push_back({ designators(ids), bp.field("value"), footprint, bp.field("mpn"), now(bp),
                         c.empty() || sameAsNow ? "" : c.front().part->id, c.empty() || sameAsNow ? "" : c.front().why });
        m_rowKeys.push_back(bp.key);
    }
    m_list->setRows(rows);
    int select = -1;
    for (size_t i = 0; i < m_rowKeys.size(); ++i)
        if (m_rowKeys[i] == keepKey) select = int(i);
    m_list->setSelectedIndex(select);
    m_choose->setEnabled(select >= 0);
    m_summary->setText(std::to_string(m_board.parts().size()) + " part(s); " + std::to_string(toChoose) + " to be chosen" +
                       (toChoose ? ", " + std::to_string(strong) + " with a strong match" : std::string()));
    const std::string best = "Use Best Matches (" + std::to_string(strong) + ")";
    m_best->setLabel(best);
    m_best->setSize(std::max(m_best->bounds().width, JButton::labelWidth(best)), btnH());   // its count is in its label
    m_best->setEnabled(strong > 0);
}

void JPlacerBoardPartsDialog::chooseSelected() {
    const int i = m_list->selectedIndex();
    if (i < 0 || size_t(i) >= m_rowKeys.size() || !m_pick) return;
    const std::vector<std::string> ids = m_board.placementsOf(m_rowKeys[size_t(i)]);
    if (ids.empty()) return;
    std::weak_ptr<bool> alive = m_alive;
    const std::string id = ids.front();
    m_pick(id, [this, alive, id](const JPPartChoice& choice) {
        if (const auto a = alive.lock(); !a || !*a) return;
        if (m_apply) m_apply(id, choice);
        m_refill = true;
    });
}

void JPlacerBoardPartsDialog::useBest() {
    // Decided first, then made: making one changes the board's parts list as it is walked.
    std::vector<std::pair<std::string, std::string>> take;   // a placement of the part, the library part
    for (const JPBoardPart& bp : m_board.parts()) {
        if (bp.state != JPBoardPart::State::Unmatched) continue;
        const std::vector<std::string> ids = m_board.placementsOf(bp.key);
        const auto c = JPPartMatcher::candidates(m_config, bp);
        if (!ids.empty() && !c.empty() && c.front().score >= JPPartMatcher::kStrong) take.emplace_back(ids.front(), c.front().part->id);
    }
    for (const auto& [id, part] : take) {
        JPPartChoice choice;
        choice.kind = JPPartChoice::Kind::Library;
        choice.libraryId = part;
        if (m_apply) m_apply(id, choice);
    }
    m_refill = true;
}

void JPlacerBoardPartsDialog::onMouse(float, float, bool pressed, bool, bool) {
    if (m_activated >= 0) {
        const int row = m_activated;
        m_activated = -1;
        if ((!pressed || JWidget::s_doubleClick) && row == m_list->selectedIndex()) chooseSelected();
    }
}

void JPlacerBoardPartsDialog::layout(float w, float h) {
    const JStyle& st = JStyle::current();
    const float pad = st.spacing * 2, cw = w - 2 * pad;
    const float buttonsY = h - pad - st.buttonHeight;
    const float top = contentTop();
    if (m_refill) {
        m_refill = false;
        fill();
    }
    m_content->setBounds({ pad, top, cw, std::max(0.f, buttonsY - st.spacing * 2 - top) });
    graph().invalidateNode(m_content->getNodeId(), DirtySelf);
    const JRect b = m_content->bounds();
    graph().computeLayout(m_content->getNodeId(), { b.width, b.width, b.height, b.height });
    m_buttons->setBounds({ pad, buttonsY, cw, st.buttonHeight });
    const float u = (cw - st.scrollBarWidth) / 20.f;   // shares of the width
    m_list->setColumnWidths({ 4 * u, 2 * u, 3 * u, 3 * u, 3 * u, 3 * u, 2 * u });
}

} // inline namespace jf
