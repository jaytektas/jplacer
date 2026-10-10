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

// The Packages tab's table, as OpenPnP's PackagesTableModel (ID,
// Description, Tape Specification, BottomVision, FiducialVision), of every
// package open (JPCatalog): the library's and each open board's, Source
// (first, its icon: the library's or a board's) saying where it lives and
// Status how it stands to the library. The library's and a board's own are
// edited in place, all but the ID; a board's copies of the library's are not.
class JPPackagesTableModel : public JPTableModel {
public:
    explicit JPPackagesTableModel(JPConfiguration& config);

    // A package changed: the board it lives in, none the library.
    std::function<void(JPBoard* board)> onChanged;
    // The rows read again from the library and the open boards.
    void reload();
    // Which rows are shown (Show beside Search): kShowAll, kShowLibrary, kShowBoards, or one board by its name.
    static constexpr const char* kShowAll = "All";
    static constexpr const char* kShowLibrary = "Library";
    static constexpr const char* kShowBoards = "Boards";
    void setShow(const std::string& show) { m_show = show; }
    bool rowShown(int row) const override;
    // What Show offers now: All, Library, Boards, then each open board.
    std::vector<std::string> showChoices() const;

    int    columnCount() const override;
    Column column(int c) const override;
    int    rowCount() const override { return int(m_rows.size()); }
    std::string text(int row, int c) const override;
    std::string rowKey(int row) const override;
    bool   editable(int row, int c) const override;
    std::string    cellIcon(int row, int c) const override;
    const uint8_t* cellTint(int row, int c) const override;
    std::string    cellTooltip(int row, int c) const override;
    std::string    displayText(int row, int c) const override;
    std::vector<std::string> choices(int row, int c) const override;
    bool   setText(int row, int c, const std::string& text, std::string& error) override;
    void   setChoice(int row, int c, int index) override;

    JPPackage*                package(int row) const;
    const JPCatalog::Package* entry(int row) const;
    int                       rowOf(const JPPackage* p) const;
    // The row of the package of id `packageId` (as kept) where it lives: a board's (its own or its copy), or
    // (no board) the library's.
    int                       rowOf(const JPBoard* board, const std::string& packageId) const;

private:
    std::vector<const JPVisionSettings*> visionChoices(JPVisionSettings::Kind kind) const;

    JPConfiguration&                m_config;
    std::vector<JPCatalog::Package> m_rows;
    std::string                  m_show = kShowAll;
};

} // inline namespace jf
