// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPTableModel.h"

#include "model/JPConfiguration.h"

#include <functional>
#include <string>
#include <vector>

inline namespace jf {

// The Vision tab's table, as OpenPnP's VisionSettingsTableModel: Name
// (changed in place, but not the stock settings') and Assigned To; only the
// settings of the kind chosen are shown.
class JPVisionSettingsTableModel : public JPTableModel {
public:
    explicit JPVisionSettingsTableModel(JPConfiguration& config);

    std::function<void()> onChanged;
    // What uses a settings, as Assigned To lists it.
    std::function<std::string(const JPVisionSettings&)> usedIn;
    void setKind(JPVisionSettings::Kind k) { m_kind = k; }

    int    columnCount() const override { return 2; }
    Column column(int c) const override;
    int    rowCount() const override;
    std::string text(int row, int c) const override;
    std::string rowKey(int row) const override;
    bool   rowShown(int row) const override;
    bool   editable(int row, int c) const override;
    bool   setText(int row, int c, const std::string& text, std::string& error) override;

    JPVisionSettings* settings(int row) const;
    int rowOf(const std::string& id) const;

private:
    JPConfiguration&       m_config;
    JPVisionSettings::Kind m_kind = JPVisionSettings::Kind::Bottom;
};

} // inline namespace jf
