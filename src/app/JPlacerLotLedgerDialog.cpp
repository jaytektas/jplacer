// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPlacerLotLedgerDialog.h"

#include "common/JPWhen.h"
#include "ui/JPUiParts.h"

#include <j/core/Dialog.h>
#include <j/core/JStyle.h>
#include <j/core/JTextHelper.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>

inline namespace jf {

namespace {

// The entries offered, in the order a lot most often meets them.
const JPLedgerEntry::Kind kOffered[] { JPLedgerEntry::Kind::Used, JPLedgerEntry::Kind::Lost, JPLedgerEntry::Kind::Counted,
                                       JPLedgerEntry::Kind::Adjusted, JPLedgerEntry::Kind::Received };

const char* quantityTip(JPLedgerEntry::Kind k) {
    switch (k) {
        case JPLedgerEntry::Kind::Counted:  return "What the lot holds, counted now";
        case JPLedgerEntry::Kind::Adjusted: return "How many more (12) or fewer (-12)";
        case JPLedgerEntry::Kind::Received: return "How many more came in";
        default:                            return "How many";
    }
}

} // namespace

JPlacerLotLedgerDialog::JPlacerLotLedgerDialog(JPStockStore& stock, const std::string& lotUuid, const std::string& partId,
                                               std::function<void()> changed, JGpuHal& hal, int sx, int sy,
                                               NativeWinHandleType parent)
    : JDialogWindow("Ledger of " + stock.lot(lotUuid).label + " (" + partId + ")", kW, kH, hal, sx, sy, parent)
    , m_stock(stock)
    , m_lotUuid(lotUuid)
    , m_changed(std::move(changed)) {
    setResizable(true, kW * 2 / 3, kH * 2 / 3);
    JSceneGraph& g = graph();
    const JStyle& st = JStyle::current();
    m_content = std::make_unique<JContainer>(g, 0.f, 0.f);
    m_content->setDirection(JFlexDirection::Column)->setAlignItems(JAlignItems::Stretch)->setGap(st.spacing);
    m_about = m_content->add(std::make_unique<JLabel>(g, "", 0.f));
    m_about->setWordWrap(true);
    m_list = m_content->add(std::make_unique<JDataGrid>(
        g, std::vector<std::string> { "When", "Entry", "Quantity", "Holds after", "Cost", "Reference", "Note" }));
    m_list->setVSizePolicy(JSizePolicyMode::Expanding, 1);

    // A new entry.
    auto row = JPUiParts::row(g);
    std::vector<std::string> kinds;
    for (JPLedgerEntry::Kind k : kOffered) kinds.push_back(JPLedgerEntry::kindName(k));
    m_kind = row->add(std::make_unique<JComboBox>(g, kinds));
    m_kind->setCurrentIndex(0);
    m_kind->setFixedSize(JTextHelper::measureWidth("Received") + st.controlHeight * 2, st.controlHeight);
    m_kind->setTooltip("Used: placed or taken for a job; Lost: dropped, mis-picked, thrown away; Counted: what it holds "
                       "now; Adjusted: more or fewer by hand; Received: more came in");
    m_kind->onIndexChanged.connect([this](int) { check(); });
    auto edit = [&](const std::string& placeholder, float columns) {
        JLineEdit* e = row->add(std::make_unique<JLineEdit>(g, placeholder));
        if (columns > 0) e->setFixedSize(JTextHelper::measureWidth("M") * columns, st.controlHeight);
        else e->setHSizePolicy(JSizePolicyMode::Expanding, 1);
        e->onTextChanged.connect([this](const std::string&) { check(); });
        e->onReturnPressed.connect([this] { addEntry(); });
        return e;
    };
    m_quantity = edit("Quantity", 7);
    m_reference = edit("Reference (job, order)", 0);
    m_note = edit("Note", 0);
    m_add = row->add(JPUiParts::button(g, "Add to Ledger"));
    m_add->onClicked.connect([this] { addEntry(); });
    m_content->add(std::move(row));
    add(m_content.get());

    m_buttons = std::make_unique<JDialogButtonBox>(g);
    m_close = m_buttons->addButton("Close the Lot", JDialogButtonBox::Role::Action);
    m_close->onClicked.connect([this] { closeOrReopen(); });
    m_buttons->addButton("Done", JDialogButtonBox::Role::Accept)->setTooltip("Every entry is kept as it is added");
    m_buttons->onAccept.connect([this] { close(); });
    m_buttons->onReject.connect([this] { close(); });
    add(m_buttons.get());
    fill();
}

void JPlacerLotLedgerDialog::fill() {
    const JPStockLot lot = m_stock.lot(m_lotUuid);
    std::string about = lot.label + (lot.packaging.empty() ? "" : ", " + lot.packaging);
    if (!lot.supplier.empty() || !lot.sku.empty()) about += ", from " + lot.supplier + (lot.sku.empty() ? "" : " " + lot.sku);
    if (!lot.dateCode.empty()) about += ", date code " + lot.dateCode;
    if (!lot.lotCode.empty()) about += ", lot code " + lot.lotCode;
    if (!lot.location.empty()) about += ", kept in " + lot.location;
    about += ". Holds " + std::to_string(lot.onHand) + (lot.closed ? "; closed (not stock), its ledger kept." : ".");
    m_about->setText(about);
    std::vector<std::vector<std::string>> rows;
    long long holds = 0;
    for (const JPLedgerEntry& e : m_stock.ledger(m_lotUuid)) {
        holds = e.apply(holds);
        char cost[32] = "";
        if (e.cost > 0) std::snprintf(cost, sizeof cost, "%.2f", e.cost);
        const std::string n = e.kind == JPLedgerEntry::Kind::Used || e.kind == JPLedgerEntry::Kind::Lost
                                  ? "-" + std::to_string(e.quantity)
                              : e.kind == JPLedgerEntry::Kind::Counted ? "= " + std::to_string(e.quantity)
                              : (e.quantity >= 0 ? "+" : "") + std::to_string(e.quantity);
        rows.push_back({ JPWhen::withAgo(e.when), JPLedgerEntry::kindName(e.kind), n, std::to_string(holds), cost, e.reference, e.note });
    }
    m_list->setRows(rows);
    m_close->setLabel(lot.closed ? "Reopen the Lot" : "Close the Lot");
    m_close->setTooltip(lot.closed ? "It is stock again" : "Used up or thrown away: no longer stock; its ledger is kept");
    check();
}

void JPlacerLotLedgerDialog::check() {
    const JPLedgerEntry::Kind k = kOffered[std::clamp(m_kind->currentIndex(), 0, int(std::size(kOffered)) - 1)];
    const std::string t = m_quantity->text();
    char* end = nullptr;
    const long long n = t.empty() ? 0 : std::strtoll(t.c_str(), &end, 10);
    const bool whole = !t.empty() && end && *end == '\0';
    const bool ok = whole && (k == JPLedgerEntry::Kind::Adjusted ? n != 0 : k == JPLedgerEntry::Kind::Counted ? n >= 0 : n > 0);
    m_quantity->setTooltip(quantityTip(k));
    m_add->setEnabled(ok);
    m_add->setTooltip(ok ? "Add it to the lot's ledger, kept at once" : std::string(quantityTip(k)) + ": a whole number");
}

void JPlacerLotLedgerDialog::addEntry() {
    if (!m_add->isEnabled()) return;
    JPLedgerEntry e;
    e.lotUuid = m_lotUuid;
    e.kind = kOffered[std::clamp(m_kind->currentIndex(), 0, int(std::size(kOffered)) - 1)];
    e.quantity = std::strtoll(m_quantity->text().c_str(), nullptr, 10);
    e.reference = m_reference->text();
    e.note = m_note->text();
    std::string error;
    if (!m_stock.addEntry(e, error)) {
        JDialog::message("Not Added", error);
        return;
    }
    m_quantity->setText("");
    m_note->setText("");
    fill();
    if (m_changed) m_changed();
}

void JPlacerLotLedgerDialog::closeOrReopen() {
    JPStockLot lot = m_stock.lot(m_lotUuid);
    lot.closed = !lot.closed;
    std::string error;
    if (!m_stock.updateLot(lot, error)) {
        JDialog::message("Not Changed", error);
        return;
    }
    fill();
    if (m_changed) m_changed();
}

void JPlacerLotLedgerDialog::layout(float w, float h) {
    const JStyle& st = JStyle::current();
    const float pad = st.spacing * 2, cw = w - 2 * pad;
    const float buttonsY = h - pad - st.buttonHeight;
    const float top = contentTop();
    m_about->setMinimumSize(0.f, std::max(st.labelHeight, m_about->heightFor(cw)));
    m_content->setBounds({ pad, top, cw, std::max(0.f, buttonsY - st.spacing * 2 - top) });
    graph().invalidateNode(m_content->getNodeId(), DirtySelf);
    const JRect b = m_content->bounds();
    graph().computeLayout(m_content->getNodeId(), { b.width, b.width, b.height, b.height });
    m_buttons->setBounds({ pad, buttonsY, cw, st.buttonHeight });
    const float when = JTextHelper::measureWidth("2026-10-08 15:00 (59 minutes ago)") + 2 * st.gridCellPadding;
    const float num = JTextHelper::measureWidth("Holds after") + 4 * st.gridCellPadding;
    const float rest = std::max(0.f, cw - when - 4 * num - st.scrollBarWidth);
    m_list->setColumnWidths({ when, num, num, num, num, rest * 0.4f, rest * 0.6f });
}

} // inline namespace jf
