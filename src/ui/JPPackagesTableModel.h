// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPTableModel.h"

#include "model/JPCatalog.h"
#include "model/JPConfiguration.h"

#include <functional>
#include <string>
#include <vector>

inline namespace jf {

// The Packages tab's table, as OpenPnP's PackagesTableModel: ID,
// Description, Tape Specification, BottomVision, FiducialVision), after
// Source (JPSourceColumn: the library's icon, or a board's for a package an
// open board's part is of); all but Source and the ID edited in place.
class JPPackagesTableModel : public JPTableModel {
public:
    explicit JPPackagesTableModel(JPConfiguration& config);

    std::function<void()> onChanged;

    int    columnCount() const override { return 6; }
    Column column(int c) const override;
    int    rowCount() const override { return int(m_config.packages().size()); }
    std::string text(int row, int c) const override;
    std::string rowKey(int row) const override;
    bool   editable(int, int c) const override { return c > 1; }   // not Source nor ID
    std::vector<std::string> choices(int row, int c) const override;
    bool   setText(int row, int c, const std::string& text, std::string& error) override;
    void   setChoice(int row, int c, int index) override;

    // The library's rows read again, with the open boards that use each (the Source column, Show).
    void reload();
    // Which rows are shown (Show beside Search): all, those an open board uses, or one board's (by its name).
    static constexpr const char* kShowAll = "All";
    static constexpr const char* kShowUsed = "Used by open boards";
    void setShow(const std::string& show) { m_show = show; }
    bool rowShown(int row) const override;
    // What Show offers now: All, Used by open boards, then each open board.
    std::vector<std::string> showChoices() const;
    std::string    cellIcon(int row, int c) const override;
    std::string    cellTooltip(int row, int c) const override;
    std::string    displayText(int row, int c) const override;

    JPPackage* package(int row) const;
    int        rowOf(const JPPackage* p) const;

private:
    std::vector<const JPVisionSettings*> visionChoices(JPVisionSettings::Kind kind) const;

    // The open boards using each row (by its place in the library), as last read.
    const std::vector<std::shared_ptr<JPBoard>>& usedBy(int row) const;

    JPConfiguration&                                   m_config;
    std::vector<std::vector<std::shared_ptr<JPBoard>>> m_usedBy;
    std::string                                        m_show = kShowAll;
};

} // inline namespace jf
