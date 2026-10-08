// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "model/JPPart.h"
#include "model/JPStockStore.h"

#include <j/app/JDialogWindow.h>
#include <j/core/JButton.h>
#include <j/core/JComboBox.h>
#include <j/core/JContainer.h>
#include <j/core/JDialogButtonBox.h>
#include <j/core/JLabel.h>
#include <j/core/JLineEdit.h>

#include <functional>
#include <memory>
#include <string>

inline namespace jf {

// Receive Stock (DESIGN.md, Stock lot): a new lot of a library part: what it is called on the shelf, how it
// comes, who it is from (the part's first offer offered), its date and lot codes, where it is kept; and how it
// came: received (an order: how many, what they cost, the order) or counted (found on the shelf). Receive keeps
// it at once, with its first ledger entry; Escape, Cancel and the [x] keep nothing.
class JPlacerStockReceiveDialog : public JDialogWindow {
public:
    static constexpr uint32_t kW = 720, kH = 470;

    JPlacerStockReceiveDialog(JPStockStore& stock, const JPPart& part, std::function<void()> received, JGpuHal& hal,
                              int sx, int sy, NativeWinHandleType parent);

protected:
    void layout(float w, float h) override;

private:
    void check();
    void receive();

    JPStockStore&         m_stock;
    std::string           m_partUuid;
    std::function<void()> m_received;

    std::unique_ptr<JContainer>       m_content;
    JLabel*                           m_note = nullptr;
    JLineEdit*                        m_label = nullptr;
    JComboBox*                        m_packaging = nullptr;
    JLineEdit*                        m_supplier = nullptr;
    JLineEdit*                        m_sku = nullptr;
    JLineEdit*                        m_dateCode = nullptr;
    JLineEdit*                        m_lotCode = nullptr;
    JLineEdit*                        m_location = nullptr;
    JLineEdit*                        m_lotNote = nullptr;
    JComboBox*                        m_how = nullptr;
    JLineEdit*                        m_quantity = nullptr;
    JLineEdit*                        m_cost = nullptr;
    JLineEdit*                        m_reference = nullptr;
    std::unique_ptr<JDialogButtonBox> m_buttons;
    JButton*                          m_receive = nullptr;
};

} // inline namespace jf
