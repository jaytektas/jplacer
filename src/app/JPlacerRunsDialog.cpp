// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPlacerRunsDialog.h"

#include "common/JPWhen.h"

#include <j/core/JStyle.h>
#include <j/core/JTextHelper.h>

#include <algorithm>
#include <filesystem>

inline namespace jf {

JPlacerRunsDialog::JPlacerRunsDialog(const JPConfiguration& config, JGpuHal& hal, int sx, int sy, NativeWinHandleType parent)
    : JDialogWindow("Runs", kW, kH, hal, sx, sy, parent) {
    setResizable(true, kW / 2, kH / 2);
    JSceneGraph& g = graph();
    const JStyle& st = JStyle::current();
    m_content = std::make_unique<JContainer>(g, 0.f, 0.f);
    m_content->setDirection(JFlexDirection::Column)->setAlignItems(JAlignItems::Stretch)->setGap(st.spacing);
    m_summary = m_content->add(std::make_unique<JLabel>(g, "", 0.f));
    m_summary->setWordWrap(true);
    m_list = m_content->add(std::make_unique<JDataGrid>(
        g, std::vector<std::string> { "Started", "Ended", "How", "Job", "Boards", "Placed", "Ledger" }));
    m_list->setVSizePolicy(JSizePolicyMode::Expanding, 1);
    add(m_content.get());

    const JPRunStore& runs = config.runs();
    const auto list = runs.runs(kShown);
    std::vector<std::vector<std::string>> rows;
    for (const JPRunStore::Run& r : list) {
        std::string boards;
        for (const JPRunStore::Board& b : r.boards)
            boards += (boards.empty() ? "" : ", ") + b.id + " " + std::filesystem::path(b.file).filename().string() +
                      (b.revision.empty() ? "" : " (" + b.revision + ")");
        rows.push_back({ JPWhen::withAgo(r.started), r.ended, JPRunStore::outcomeName(r.outcome),
                         r.job.empty() ? std::string("(not saved)") : std::filesystem::path(r.job).filename().string(), boards,
                         std::to_string(runs.placedCount(r.uuid)),
                         r.ledgered ? "written" : r.outcome == JPRunStore::Outcome::Open ? "when it ends" : "not yet" });
    }
    m_list->setRows(rows);
    m_summary->setText(list.empty() ? "No runs yet: each Start of a job is a run, recorded as it goes."
                                    : "The newest " + std::to_string(list.size()) + " run(s). A run's parts are written "
                                      "to the stock's ledger when it ends: placed as Used, fed and not placed as Lost.");

    m_buttons = std::make_unique<JDialogButtonBox>(g);
    m_buttons->addButton("Close", JDialogButtonBox::Role::Accept);
    m_buttons->onAccept.connect([this] { close(); });
    m_buttons->onReject.connect([this] { close(); });
    add(m_buttons.get());
}

void JPlacerRunsDialog::layout(float w, float h) {
    const JStyle& st = JStyle::current();
    const float pad = st.spacing * 2, cw = w - 2 * pad;
    const float buttonsY = h - pad - st.buttonHeight;
    const float top = contentTop();
    m_summary->setMinimumSize(0.f, std::max(st.labelHeight, m_summary->heightFor(cw)));
    m_content->setBounds({ pad, top, cw, std::max(0.f, buttonsY - st.spacing * 2 - top) });
    graph().invalidateNode(m_content->getNodeId(), DirtySelf);
    const JRect b = m_content->bounds();
    graph().computeLayout(m_content->getNodeId(), { b.width, b.width, b.height, b.height });
    m_buttons->setBounds({ pad, buttonsY, cw, st.buttonHeight });
    auto fit = [&st](const char* text) { return JTextHelper::measureWidth(text) + 4 * st.gridCellPadding; };
    const float started = fit("2026-10-08 15:00:00 (59 minutes ago)"), ended = fit("2026-10-08 15:00:00");
    const float how = fit("Interrupted"), placed = fit("Placed"), ledger = fit("when it ends");
    const float rest = std::max(0.f, cw - started - ended - how - placed - ledger - st.scrollBarWidth);
    m_list->setColumnWidths({ started, ended, how, rest * 0.3f, rest * 0.7f, placed, ledger });
}

} // inline namespace jf
