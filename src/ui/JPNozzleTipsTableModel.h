// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPTableModel.h"

#include "model/JPPackage.h"

#include <functional>
#include <string>
#include <utility>
#include <vector>

inline namespace jf {

// A package's Nozzle Tips tab, as OpenPnP's PackageNozzleTipsWizard: the
// machine's nozzle tips (Nozzle Tip) and whether each can pick the package
// (Compatible, ticked in place).
class JPNozzleTipsTableModel : public JPTableModel {
public:
    // The machine's tips, id and name.
    void setTips(std::vector<std::pair<std::string, std::string>> tips) { m_tips = std::move(tips); }
    void setPackage(JPPackage* package) { m_package = package; }
    std::function<void()> onChanged;

    int    columnCount() const override { return 2; }
    Column column(int c) const override {
        return c == 0 ? Column { "Nozzle Tip", "", Kind::Text } : Column { "Compatible", "", Kind::Boolean };
    }
    int    rowCount() const override { return m_package ? int(m_tips.size()) : 0; }
    std::string text(int row, int c) const override { return c == 0 ? m_tips[size_t(row)].second : std::string(); }
    bool   checked(int row, int) const override;
    std::string rowKey(int row) const override { return m_tips[size_t(row)].first; }
    bool   editable(int, int c) const override { return c == 1; }
    void   setChecked(int row, int c, bool on) override;

private:
    std::vector<std::pair<std::string, std::string>> m_tips;
    JPPackage*                                       m_package = nullptr;
};

} // inline namespace jf
