// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPlacerPlanDialog.h"

#include "ui/JPUiParts.h"

#include <j/core/JStyle.h>
#include <j/core/JTextHelper.h>

#include <algorithm>
#include <cstdio>
#include <iterator>

inline namespace jf {

JPlacerPlanDialog::JPlacerPlanDialog(JPConfiguration& config, JPJob& job, std::function<void()> changed, JGpuHal& hal,
                                     int sx, int sy, NativeWinHandleType parent)
    : JDialogWindow("Plan", kW, kH, hal, sx, sy, parent)
    , m_config(config)
    , m_job(job)
    , m_changed(std::move(changed)) {
    setResizable(true, kW / 2, kH / 2);
    JSceneGraph& g = graph();
    const JStyle& st = JStyle::current();
    m_content = std::make_unique<JContainer>(g, 0.f, 0.f);
    m_content->setDirection(JFlexDirection::Column)->setAlignItems(JAlignItems::Stretch)->setGap(st.spacing);
    m_summary = m_content->add(std::make_unique<JLabel>(g, "", 0.f));
    m_summary->setWordWrap(true);

    auto bar = JPUiParts::row(g);
    JLabel* sortLabel = bar->add(std::make_unique<JLabel>(g, "Order by", 0.f));
    sortLabel->setFixedSize(JTextHelper::measureWidth("Order by") + st.spacing * 2, st.controlHeight);
    m_sort = bar->add(std::make_unique<JComboBox>(g, std::vector<std::string>(std::begin(JPJobPlan::kSorts), std::end(JPJobPlan::kSorts))));
    m_sort->setFixedSize(JTextHelper::measureWidth("Machine's job order") + st.controlHeight * 2, st.controlHeight);
    const auto sorts = std::vector<std::string>(std::begin(JPJobPlan::kSorts), std::end(JPJobPlan::kSorts));
    m_sort->setCurrentIndex(int(std::find(sorts.begin(), sorts.end(), JPJobPlan::sortOf(job)) - sorts.begin()));
    m_sort->setTooltip("Machine's job order: as Machine Setup's Job Order says. Height: the lowest first, tall parts "
                       "last. Package size: the smallest first. Most first: the groups with the most placements. Name.");
    m_sort->onIndexChanged.connect([this, sorts](int i) {
        if (i < 0 || size_t(i) >= sorts.size()) return;
        m_job.planSort = sorts[size_t(i)] == JPJobPlan::kMachine ? std::string() : sorts[size_t(i)];
        m_job.planOrder.clear();
        if (m_changed) m_changed();
        fill(0);
    });
    bar->add(std::make_unique<JContainer>(g, 0.f, 0.f))->setHSizePolicy(JSizePolicyMode::Expanding, 1);
    m_up = bar->add(JPUiParts::button(g, "Move Up"));
    m_up->setTooltip("Place this group before the one above it");
    m_up->onClicked.connect([this] { move(-1); });
    m_down = bar->add(JPUiParts::button(g, "Move Down"));
    m_down->setTooltip("Place this group after the one below it");
    m_down->onClicked.connect([this] { move(1); });
    m_again = bar->add(JPUiParts::button(g, "Sort Again"));
    m_again->setTooltip("Drop the order set by hand: every group in the sort's order");
    m_again->onClicked.connect([this] {
        m_job.planOrder.clear();
        if (m_changed) m_changed();
        fill(m_list->selectedIndex());
    });
    m_content->add(std::move(bar));

    m_list = m_content->add(std::make_unique<JDataGrid>(
        g, std::vector<std::string> { "#", "Part", "Left", "Height", "Package", "Comes from" }));
    m_list->setVSizePolicy(JSizePolicyMode::Expanding, 1);
    m_list->onSelectionChanged.connect([this](int i) {
        m_up->setEnabled(i > 0);
        m_down->setEnabled(i >= 0 && size_t(i) + 1 < m_groups.size());
    });
    add(m_content.get());

    m_buttons = std::make_unique<JDialogButtonBox>(g);
    m_buttons->addButton("Close", JDialogButtonBox::Role::Accept)->setTooltip("The plan is kept with the job as it is changed");
    m_buttons->onAccept.connect([this] { close(); });
    m_buttons->onReject.connect([this] { close(); });
    add(m_buttons.get());
    fill(-1);
}

void JPlacerPlanDialog::fill(int select) {
    m_groups = JPJobPlan::groups(m_config, m_job);
    std::vector<std::vector<std::string>> rows;
    int placements = 0, loads = 0;
    for (size_t i = 0; i < m_groups.size(); ++i) {
        const JPJobPlan::Group& gr = m_groups[i];
        placements += gr.left;
        loads += gr.feederName.empty();
        char height[32] = "not known";
        if (gr.heightMm > 0) std::snprintf(height, sizeof height, "%.2f mm", gr.heightMm);
        rows.push_back({ std::to_string(i + 1), gr.partId, std::to_string(gr.left), height, gr.packageId,
                         gr.feederName.empty() ? std::string("a load (asked for when the run gets to it)") : gr.feederName });
    }
    m_list->setRows(rows);
    const int pick = select >= 0 && size_t(select) < m_groups.size() ? select : -1;
    m_list->setSelectedIndex(pick);
    m_up->setEnabled(pick > 0);
    m_down->setEnabled(pick >= 0 && size_t(pick) + 1 < m_groups.size());
    m_again->setEnabled(!m_job.planOrder.empty());
    const bool machine = JPJobPlan::sortOf(m_job) == JPJobPlan::kMachine && m_job.planOrder.empty();
    std::string s = std::to_string(m_groups.size()) + " part(s), " + std::to_string(placements) + " placement(s) left to place; " +
                    std::to_string(loads) + " load(s) the run will ask for. ";
    s += machine ? "The run orders them as Machine Setup's Job Order says; the groups are listed by name. Choose an order, "
                   "or move a group, for the run to place group by group."
                 : "The run places them group by group, in this order" +
                       std::string(m_job.planOrder.empty() ? "." : ", some set by hand (Sort Again drops those).");
    m_summary->setText(s);
}

void JPlacerPlanDialog::move(int by) {
    const int i = m_list->selectedIndex();
    const int to = i + by;
    if (i < 0 || to < 0 || size_t(i) >= m_groups.size() || size_t(to) >= m_groups.size()) return;
    JPJobPlan::move(m_job, m_groups, size_t(i), size_t(to));
    if (m_changed) m_changed();
    fill(to);
}

void JPlacerPlanDialog::layout(float w, float h) {
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
    const float n = fit("000"), left = fit("Left"), height = fit("not known");
    const float rest = std::max(0.f, cw - n - left - height - st.scrollBarWidth);
    m_list->setColumnWidths({ n, rest * 0.35f, left, height, rest * 0.2f, rest * 0.45f });
}

} // inline namespace jf
