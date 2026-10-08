// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPlacerStockReceiveDialog.h"

#include "ui/JPFieldGrid.h"

#include <j/core/Dialog.h>
#include <j/core/JStyle.h>

#include <algorithm>
#include <cstdlib>
#include <iterator>

inline namespace jf {

namespace {

// A whole number written in `text`, nothing else; false when there is none.
bool count(const std::string& text, long long& out) {
    if (text.empty()) return false;
    char* end = nullptr;
    out = std::strtoll(text.c_str(), &end, 10);
    return end && *end == '\0';
}

} // namespace

JPlacerStockReceiveDialog::JPlacerStockReceiveDialog(JPStockStore& stock, const JPPart& part, std::function<void()> received,
                                                     JGpuHal& hal, int sx, int sy, NativeWinHandleType parent)
    : JDialogWindow("Receive Stock of " + part.id, kW, kH, hal, sx, sy, parent)
    , m_stock(stock)
    , m_partUuid(part.uuid)
    , m_received(std::move(received)) {
    setResizable(true, kW * 2 / 3, kH * 2 / 3);
    JSceneGraph& g = graph();
    const JStyle& st = JStyle::current();
    m_content = std::make_unique<JContainer>(g, 0.f, 0.f);
    m_content->setDirection(JFlexDirection::Column)->setAlignItems(JAlignItems::Stretch)->setGap(st.spacing);
    m_note = m_content->add(std::make_unique<JLabel>(g, "", 0.f));
    m_note->setWordWrap(true);

    auto field = [&](JPFieldGrid& grid, const std::string& label, const std::string& tip, const std::string& value) {
        auto e = std::make_unique<JLineEdit>(g, "");
        e->setText(value);
        e->setTooltip(tip);
        JLineEdit* raw = grid.widget(label, std::move(e));
        raw->onTextChanged.connect([this](const std::string&) { check(); });
        return raw;
    };
    // The lot: its offer's supplier and packaging offered.
    const JPPart::Offer* offer = part.offers.empty() ? nullptr : &part.offers.front();
    auto lotGrid = std::make_unique<JPFieldGrid>(g, 2);
    m_label = field(*lotGrid, "Lot", "What it is called on the shelf (reel A, the bag in drawer 2)", "");
    std::vector<std::string> kinds(std::begin(JPPart::kPackagingKinds), std::end(JPPart::kPackagingKinds));
    std::string kind = offer && !offer->packaging.empty() ? offer->packaging
                       : !part.packagings.empty()        ? part.packagings.front().kind
                                                         : kinds.front();
    const auto k = std::find(kinds.begin(), kinds.end(), kind);
    m_packaging = lotGrid->widget("Packaging", std::make_unique<JComboBox>(g, kinds));
    m_packaging->setCurrentIndex(k == kinds.end() ? 0 : int(k - kinds.begin()));
    m_packaging->setTooltip("How this lot comes");
    m_supplier = field(*lotGrid, "Supplier", "Who it came from", offer ? offer->supplier : std::string());
    m_sku = field(*lotGrid, "SKU", "The supplier's part number", offer ? offer->sku : std::string());
    m_dateCode = field(*lotGrid, "Date code", "The maker's date code, as on the label", "");
    m_lotCode = field(*lotGrid, "Lot code", "The maker's lot code, as on the label", "");
    m_location = field(*lotGrid, "Where kept", "Where it is kept (shelf 3, drawer 12)", "");
    m_lotNote = field(*lotGrid, "Note", "Anything else about it", "");
    m_content->add(JPFieldGrid::grouped(g, "The Lot", std::move(lotGrid)));

    // How it came: the first entry of its ledger.
    auto cameGrid = std::make_unique<JPFieldGrid>(g, 2);
    m_how = cameGrid->widget("How", std::make_unique<JComboBox>(g, std::vector<std::string> { "Received", "Counted" }));
    m_how->setCurrentIndex(0);
    m_how->setTooltip("Received: an order came in; Counted: a lot found and counted (no order)");
    m_how->onIndexChanged.connect([this](int) { check(); });
    m_quantity = field(*cameGrid, "Quantity", "How many parts it holds", "");
    m_cost = field(*cameGrid, "Cost", "What they cost, all of them (empty: not kept)", "");
    m_reference = field(*cameGrid, "Order", "The order or invoice it came on", "");
    m_content->add(JPFieldGrid::grouped(g, "How It Came", std::move(cameGrid)));
    add(m_content.get());

    m_buttons = std::make_unique<JDialogButtonBox>(g);
    m_buttons->addButton("Cancel", JDialogButtonBox::Role::Reject)->setTooltip("Keep nothing");
    m_receive = m_buttons->addButton("Receive", JDialogButtonBox::Role::Accept);
    m_buttons->onReject.connect([this] { close(); });
    m_buttons->onAccept.connect([this] { receive(); });
    add(m_buttons.get());
    check();
}

void JPlacerStockReceiveDialog::check() {
    long long n = 0;
    const bool counted = count(m_quantity->text(), n) && n > 0;
    char* end = nullptr;
    const std::string c = m_cost->text();
    const bool costed = c.empty() || (std::strtod(c.c_str(), &end) >= 0 && end && *end == '\0');
    const bool labelled = !m_label->text().empty();
    m_receive->setEnabled(counted && costed && labelled);
    const std::string why = !labelled ? "Give the lot a name it is known by on the shelf."
                            : !counted ? "Say how many parts it holds."
                            : !costed  ? "The cost is a number (all of them), or empty."
                                       : "Receive keeps the lot and its first ledger entry at once.";
    m_note->setText(why);
    m_receive->setTooltip(why);
}

void JPlacerStockReceiveDialog::receive() {
    if (!m_receive->isEnabled()) return;
    JPStockLot lot;
    lot.partUuid = m_partUuid;
    lot.label = m_label->text();
    lot.packaging = m_packaging->currentText();
    lot.supplier = m_supplier->text();
    lot.sku = m_sku->text();
    lot.dateCode = m_dateCode->text();
    lot.lotCode = m_lotCode->text();
    lot.location = m_location->text();
    lot.note = m_lotNote->text();
    JPLedgerEntry first;
    first.kind = m_how->currentIndex() == 1 ? JPLedgerEntry::Kind::Counted : JPLedgerEntry::Kind::Received;
    count(m_quantity->text(), first.quantity);
    first.cost = m_cost->text().empty() ? 0 : std::strtod(m_cost->text().c_str(), nullptr);
    first.reference = m_reference->text();
    std::string error;
    if (!m_stock.addLot(lot, first, error)) {
        JDialog::message("Stock Not Received", error);
        return;
    }
    close();
    if (m_received) m_received();
}

void JPlacerStockReceiveDialog::layout(float w, float h) {
    const JStyle& st = JStyle::current();
    const float pad = st.spacing * 2, cw = w - 2 * pad;
    const float buttonsY = h - pad - st.buttonHeight;
    const float top = contentTop();
    m_note->setMinimumSize(0.f, std::max(st.labelHeight, m_note->heightFor(cw)));
    m_content->setBounds({ pad, top, cw, std::max(0.f, buttonsY - st.spacing * 2 - top) });
    graph().invalidateNode(m_content->getNodeId(), DirtySelf);
    const JRect b = m_content->bounds();
    graph().computeLayout(m_content->getNodeId(), { b.width, b.width, 0.f, b.height });
    m_buttons->setBounds({ pad, buttonsY, cw, st.buttonHeight });
}

} // inline namespace jf
