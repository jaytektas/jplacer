// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPlacerShortagesDialog.h"

#include "model/JPShortages.h"

#include <j/core/JStyle.h>
#include <j/core/JTextHelper.h>

#include <algorithm>
#include <cstdio>

inline namespace jf {

JPlacerShortagesDialog::JPlacerShortagesDialog(const JPConfiguration& config, const JPJob& job, JGpuHal& hal, int sx, int sy,
                                               NativeWinHandleType parent)
    : JDialogWindow("Shortages", kW, kH, hal, sx, sy, parent) {
    setResizable(true, kW / 2, kH / 2);
    JSceneGraph& g = graph();
    const JStyle& st = JStyle::current();
    m_content = std::make_unique<JContainer>(g, 0.f, 0.f);
    m_content->setDirection(JFlexDirection::Column)->setAlignItems(JAlignItems::Stretch)->setGap(st.spacing);
    m_summary = m_content->add(std::make_unique<JLabel>(g, "", 0.f));
    m_summary->setWordWrap(true);
    m_list = m_content->add(std::make_unique<JDataGrid>(
        g, std::vector<std::string> { "Part", "Left to place", "Attrition", "In stock", "Short", "Lots (where kept: holds)", "Buy from" }));
    m_list->setVSizePolicy(JSizePolicyMode::Expanding, 1);
    add(m_content.get());

    const auto lines = JPShortages::of(config, job);
    size_t shortParts = 0, notLibrary = 0;
    int placements = 0;
    std::vector<std::vector<std::string>> rows;
    for (const JPShortages::Line& l : lines) {
        placements += l.needed;
        if (l.partUuid.empty()) {
            ++notLibrary;
            rows.push_back({ l.partId, std::to_string(l.needed), "", "", "", "not in the library (no stock kept)", "" });
            continue;
        }
        shortParts += l.shortBy > 0;
        char rate[64];
        if (l.rateFrom == JPShortages::Line::Rate::Unknown) std::snprintf(rate, sizeof rate, "not known");
        else
            std::snprintf(rate, sizeof rate, "+%d (%.1f %%, %s)", l.attrition, l.rate * 100,
                          l.rateFrom == JPShortages::Line::Rate::Set ? "set" : "measured");
        std::string lots;
        for (const JPStockLot& lot : l.lots)
            lots += (lots.empty() ? "" : "; ") + lot.label + (lot.location.empty() ? "" : " (" + lot.location + ")") + ": " +
                    std::to_string(lot.onHand);
        std::string buy;
        if (l.shortBy > 0)
            if (const JPPart* part = config.libraryPart(l.partId))
                for (const JPPart::Offer& o : part->offers)
                    buy += (buy.empty() ? "" : "; ") + o.supplier + (o.sku.empty() ? "" : " " + o.sku);
        rows.push_back({ l.partId, std::to_string(l.needed), rate, std::to_string(l.inStock),
                         l.shortBy > 0 ? std::to_string(l.shortBy) : "", lots, buy });
    }
    m_list->setRows(rows);
    std::string summary = std::to_string(placements) + " placement(s) left to place, of " + std::to_string(lines.size()) + " part(s)";
    if (lines.size() > notLibrary) summary += ": " + (shortParts == 0 ? std::string("none short") : std::to_string(shortParts) + " short");
    if (notLibrary > 0) summary += "; " + std::to_string(notLibrary) + " not in the library (no stock kept)";
    summary += ". Stock never stops a run: what the feeders hold is placed, and the rest is asked for as the run reaches it.";
    m_summary->setText(summary);

    m_buttons = std::make_unique<JDialogButtonBox>(g);
    m_buttons->addButton("Close", JDialogButtonBox::Role::Accept);
    m_buttons->onAccept.connect([this] { close(); });
    m_buttons->onReject.connect([this] { close(); });
    add(m_buttons.get());
}

void JPlacerShortagesDialog::layout(float w, float h) {
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
    auto fit = [&st](const char* header) { return JTextHelper::measureWidth(header) + 4 * st.gridCellPadding; };
    const float left = fit("Left to place"), inStock = fit("In stock"), shortBy = fit("Short");
    const float rate = fit("+00 (10.0 %, measured)");
    const float rest = std::max(0.f, cw - left - rate - inStock - shortBy - st.scrollBarWidth);
    m_list->setColumnWidths({ rest * 0.25f, left, rate, inStock, shortBy, rest * 0.5f, rest * 0.25f });
}

} // inline namespace jf
