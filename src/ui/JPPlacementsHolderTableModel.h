// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPTableModel.h"

#include "model/JPConfiguration.h"
#include "model/JPJob.h"

#include <functional>

inline namespace jf {

// The Boards (or Panels) tab's table of the boards (or panels) known, as
// OpenPnP's PlacementsHolderTableModel: Board Name (or Panel Name), Width
// and Length, all edited in place; the name's tooltip is the file. A
// change is made to the definition and carried to its uses.
class JPPlacementsHolderTableModel : public JPTableModel {
public:
    JPPlacementsHolderTableModel(JPConfiguration& config, JPPlacementsHolder::Kind kind,
                                 std::function<const JPJob*()> job);

    std::function<void()> onChanged;

    int    columnCount() const override { return 3; }
    Column column(int c) const override;
    int    rowCount() const override;
    std::string text(int row, int c) const override;
    std::string rowKey(int row) const override;
    std::optional<int> compare(int a, int b, int c) const override;
    std::string cellTooltip(int row, int c) const override;
    bool   editable(int, int) const override { return true; }
    bool   setText(int row, int c, const std::string& text, std::string& error) override;

    JPPlacementsHolder* holder(int row) const;
    int                 rowOf(const JPPlacementsHolder* h) const;

private:
    JPConfiguration&              m_config;
    JPPlacementsHolder::Kind      m_kind;
    std::function<const JPJob*()> m_job;
};

} // inline namespace jf
