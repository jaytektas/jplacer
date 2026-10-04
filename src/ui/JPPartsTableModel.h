// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPTableModel.h"

#include "model/JPConfiguration.h"

#include <functional>
#include <string>
#include <vector>

inline namespace jf {

// The Parts tab's table, as OpenPnP's PartsTableModel: ID, Description,
// Height, Through-Board Depth, Package, Speed %, BottomVision,
// FiducialVision, Placements, Feeders. All but the ID and the two counts
// are edited in place.
class JPPartsTableModel : public JPTableModel {
public:
    explicit JPPartsTableModel(JPConfiguration& config);

    // A part changed (to be saved, and other views told).
    std::function<void()> onChanged;

    int    columnCount() const override;
    Column column(int c) const override;
    int    rowCount() const override;
    std::string text(int row, int c) const override;
    double number(int row, int c) const override;
    std::string rowKey(int row) const override;
    bool   editable(int row, int c) const override;
    std::vector<std::string> choices(int row, int c) const override;
    bool   setText(int row, int c, const std::string& text, std::string& error) override;
    void   setChoice(int row, int c, int index) override;

    JPPart* part(int row) const;
    int     rowOf(const JPPart* part) const;

private:
    // The vision settings a column offers, sorted by name, then none.
    std::vector<const JPVisionSettings*> visionChoices(JPVisionSettings::Kind kind) const;
    std::vector<const JPPackage*>        packageChoices() const;

    JPConfiguration& m_config;
};

} // inline namespace jf
