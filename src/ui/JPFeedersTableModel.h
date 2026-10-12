// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPBatchedChange.h"
#include "JPTableModel.h"

#include "model/JPConfiguration.h"

#include <functional>
#include <string>
#include <vector>

inline namespace jf {

// The Feeders tab's table, as OpenPnP's FeedersTableModel shown with its
// Type column moved after Part, after its ID (a GUID): ID, Name, Part, Type,
// Priority, Faults, Enabled, Feed. Name, Priority, Enabled and Feed are edited in place (Feed
// where the feeder's kind offers feed options). Priority and Feed sort in
// their enums' order, as Swing sorts an enum.
class JPFeedersTableModel : public JPTableModel {
public:
    explicit JPFeedersTableModel(JPConfiguration& config);

    // A feeder changed in the table.
    std::function<void()> onChanged;
    // Whether an enabled placement of an enabled board in the job uses a
    // part (else the feeder's Enabled is greyed, as OpenPnP greys it).
    std::function<bool(const std::string& partId)> partUsed;

    int    columnCount() const override;
    Column column(int c) const override;
    int    rowCount() const override;
    std::string text(int row, int c) const override;
    bool   checked(int row, int c) const override;
    std::string rowKey(int row) const override;
    std::optional<int> compare(int rowA, int rowB, int c) const override;
    bool   cellDimmed(int row, int c) const override;
    bool   editable(int row, int c) const override;
    std::vector<std::string> choices(int row, int c) const override;
    bool   setText(int row, int c, const std::string& text, std::string& error) override;
    void   setChoice(int row, int c, int index) override;
    void   setChecked(int row, int c, bool on) override;

    JPFeeder* feeder(int row) const;
    int       rowOf(const std::string& feederId) const;

    std::optional<JPFieldSet::Of> fieldsOf() const override { return JPFieldSet::Of::Feeders; }
    const JPFieldValues* custom(int row) const override;
    bool   setCustom(int row, const std::string& key, const JJson& value) override;
    void   beginEdits() override { m_said.begin(); }
    void   endEdits() override { m_said.end(onChanged); }

private:
    Column heading(int c) const;
    // The field a column shows (JPFieldSet's built-in's key).
    std::string keyAt(int c) const;
    JPBatchedChange m_said;
    JPConfiguration& m_config;
};

} // inline namespace jf
