// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPTableModel.h"

#include "model/JPConfiguration.h"
#include "model/JPJob.h"

#include <functional>

inline namespace jf {

// A board's (or panel's) placements, as OpenPnP's
// PlacementsHolderPlacementsTableModel: Enabled, ID, Part, Side, X, Y, Rot.,
// Type, Placed, Status, Error Handling, Rank, Comments; a tab shows the
// columns it wants (the Boards tab leaves out Placed and Status). All but
// the ID and Status are edited in place, on the definition, and carried to
// its uses. IDs sort as reference designators (R2 before R10); a
// fiducial's type stands out.
class JPPlacementsTableModel : public JPTableModel {
public:
    enum Col { kEnabled, kId, kPart, kSide, kX, kY, kRotation, kType, kPlaced, kStatus, kErrorHandling, kRank,
               kComments, kColumns };

    JPPlacementsTableModel(JPConfiguration& config, std::function<const JPJob*()> job, std::vector<Col> shown);

    std::function<void()> onChanged;

    // The holder whose placements are shown (a definition); null: none.
    void setHolder(JPPlacementsHolder* holder) { m_holder = holder; }
    JPPlacementsHolder* holder() const { return m_holder; }
    JPPlacement* placement(int row) const;
    int          rowOf(const std::string& id) const;
    // The model column showing `c`, or -1.
    int columnOf(Col c) const;

    int    columnCount() const override { return int(m_shown.size()); }
    Column column(int c) const override;
    int    rowCount() const override;
    std::string text(int row, int c) const override;
    bool   checked(int row, int c) const override;
    double number(int row, int c) const override;
    std::string rowKey(int row) const override;
    std::optional<int> compare(int a, int b, int c) const override;
    std::string cellTooltip(int row, int c) const override;
    bool   highlighted(int row, int c) const override;
    bool   editable(int, int c) const override;
    std::vector<std::string> choices(int row, int c) const override;
    bool   setText(int row, int c, const std::string& text, std::string& error) override;
    void   setChoice(int row, int c, int index) override;
    void   setChecked(int row, int c, bool on) override;

    // `set` applied to the placement `id`, on the definition and its uses.
    void edit(const std::string& id, const std::function<void(JPPlacement&)>& set);

    // OpenPnP's order for reference designators: those with no digit first,
    // then by the letters before the digits, then by the number.
    static int compareReferences(const std::string& a, const std::string& b);

private:
    std::vector<const JPPart*> partChoices() const;

    JPConfiguration&              m_config;
    std::function<const JPJob*()> m_job;
    std::vector<Col>              m_shown;
    JPPlacementsHolder*           m_holder = nullptr;
};

} // inline namespace jf
