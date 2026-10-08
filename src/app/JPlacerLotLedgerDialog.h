// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "model/JPStockStore.h"

#include <j/app/JDialogWindow.h>
#include <j/core/JButton.h>
#include <j/core/JComboBox.h>
#include <j/core/JContainer.h>
#include <j/core/JDataGrid.h>
#include <j/core/JDialogButtonBox.h>
#include <j/core/JLabel.h>
#include <j/core/JLineEdit.h>

#include <functional>
#include <memory>
#include <string>

inline namespace jf {

// A stock lot's ledger (DESIGN.md, Stock ledger): the lot, and every change to it in order with what it held
// after each, so a wrong figure shows why; an entry added (used, lost, counted, adjusted, received more), kept at
// once; Close the Lot (used up or thrown away: no longer stock, its ledger kept) and Reopen the Lot.
class JPlacerLotLedgerDialog : public JDialogWindow {
public:
    static constexpr uint32_t kW = 900, kH = 560;

    // `changed`: an entry added, or the lot closed or reopened.
    JPlacerLotLedgerDialog(JPStockStore& stock, const std::string& lotUuid, const std::string& partId,
                           std::function<void()> changed, JGpuHal& hal, int sx, int sy, NativeWinHandleType parent);

protected:
    void layout(float w, float h) override;

private:
    void fill();
    void check();
    void addEntry();
    void closeOrReopen();

    JPStockStore&         m_stock;
    std::string           m_lotUuid;
    std::function<void()> m_changed;

    std::unique_ptr<JContainer>       m_content;
    JLabel*                           m_about = nullptr;
    JDataGrid*                        m_list = nullptr;
    JComboBox*                        m_kind = nullptr;
    JLineEdit*                        m_quantity = nullptr;
    JLineEdit*                        m_reference = nullptr;
    JLineEdit*                        m_note = nullptr;
    JButton*                          m_add = nullptr;
    std::unique_ptr<JDialogButtonBox> m_buttons;
    JButton*                          m_close = nullptr;
};

} // inline namespace jf
