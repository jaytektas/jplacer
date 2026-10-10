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

// The Parts tab's table, as OpenPnP's PartsTableModel (ID, Description,
// Height, Through-Board Depth, Package, Speed %, BottomVision,
// FiducialVision, Placements, Feeders), of every part open (JPCatalog): the
// library's and each open board's, Source (first, its icon: the library's
// or a board's) saying where it lives and Status how it stands to the
// library. The library's and a board's own are edited in place, all but the
// ID and the two counts; a board's copies of the library's are not.
class JPPartsTableModel : public JPTableModel {
public:
    explicit JPPartsTableModel(JPConfiguration& config);

    // A part changed (to be saved, and other views told): the board it lives in, none the library.
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
    int    rowCount() const override;
    std::string text(int row, int c) const override;
    double number(int row, int c) const override;
    std::string rowKey(int row) const override;
    bool   editable(int row, int c) const override;
    std::vector<std::string> choices(int row, int c) const override;
    bool   setText(int row, int c, const std::string& text, std::string& error) override;
    void   setChoice(int row, int c, int index) override;
    std::string    cellIcon(int row, int c) const override;
    const uint8_t* cellTint(int row, int c) const override;
    std::string    cellTooltip(int row, int c) const override;
    std::string    displayText(int row, int c) const override;

    // A row's part (none: a board's part to be chosen), and the whole entry.
    JPPart*                 part(int row) const;
    const JPCatalog::Part*  entry(int row) const;
    int                     rowOf(const JPPart* part) const;
    // A board's part's row (its board part by key), else (no board) the library part's.
    int                     rowOf(const JPBoard* board, const std::string& boardPartKey, const JPPart* libraryPart) const;

private:
    // The vision settings a column offers, sorted by name, then none.
    std::vector<const JPVisionSettings*> visionChoices(JPVisionSettings::Kind kind) const;
    // The packages a row's part may take: the library's, and a board's own part its board's own too.
    std::vector<const JPPackage*>        packageChoices(int row) const;

    JPConfiguration&             m_config;
    std::vector<JPCatalog::Part> m_rows;
    std::string                  m_show = kShowAll;
};

} // inline namespace jf
