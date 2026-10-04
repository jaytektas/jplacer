// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPTableModel.h"

#include "model/JPConfiguration.h"

#include <functional>
#include <string>
#include <vector>

inline namespace jf {

// The Packages tab's table, as OpenPnP's PackagesTableModel: ID,
// Description, Tape Specification, BottomVision, FiducialVision; all but
// the ID edited in place.
class JPPackagesTableModel : public JPTableModel {
public:
    explicit JPPackagesTableModel(JPConfiguration& config);

    std::function<void()> onChanged;

    int    columnCount() const override { return 5; }
    Column column(int c) const override;
    int    rowCount() const override { return int(m_config.packages().size()); }
    std::string text(int row, int c) const override;
    std::string rowKey(int row) const override;
    bool   editable(int, int c) const override { return c != 0; }
    std::vector<std::string> choices(int row, int c) const override;
    bool   setText(int row, int c, const std::string& text, std::string& error) override;
    void   setChoice(int row, int c, int index) override;

    JPPackage* package(int row) const;
    int        rowOf(const JPPackage* p) const;

private:
    std::vector<const JPVisionSettings*> visionChoices(JPVisionSettings::Kind kind) const;

    JPConfiguration& m_config;
};

} // inline namespace jf
