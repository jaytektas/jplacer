// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPTableModel.h"

#include "model/JPCatalog.h"
#include "model/JPConfiguration.h"
#include "setup/JPLibrarySearch.h"

#include <functional>
#include <map>
#include <string>
#include <utility>
#include <vector>

inline namespace jf {

// The Parts tab's table, as OpenPnP's PartsTableModel: ID, its Name (what it
// is shown as, edited in place and unique), Description,
// Height, Through-Board Depth, Package, Speed %, BottomVision,
// FiducialVision, Placements, Feeders), the library's parts (the only ones),
// after Source (JPSourceColumn: the library's icon, or a board's for a part
// an open board uses). All but Source, the ID, the MPN and the counts are edited
// in place. Then more about each part, hidden until chosen (a table's
// Columns…): its Packaging, Suppliers, Manufacturer, Stock and Datasheet,
// each cell how many and the first of them (all of them when pointed at,
// and all of them searched and sorted by).
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
    std::optional<JComboBox::Search> choiceSearch(int row, int c) const override;
    bool   setText(int row, int c, const std::string& text, std::string& error) override;
    void   setChoice(int row, int c, int index) override;

    // The library's rows read again, with the open boards that use each (the Source column, Show).
    void reload();
    // Which rows are shown (Show beside Search): all, those an open board uses, or one board's (by its name).
    static constexpr const char* kShowAll = JPLibrarySearch::kShowAll;
    static constexpr const char* kShowUsed = JPLibrarySearch::kShowUsed;
    void setShow(const std::string& show) { m_show = show; }
    bool rowShown(int row) const override;
    // What Show offers now: All, Used by open boards, then each open board.
    std::vector<std::string> showChoices() const;
    std::string    cellIcon(int row, int c) const override;
    std::string    cellTooltip(int row, int c) const override;
    std::string    displayText(int row, int c) const override;

    JPPart* part(int row) const;
    int     rowOf(const JPPart* part) const;

private:
    // The vision settings a column offers, sorted by name, then none.
    std::vector<const JPVisionSettings*> visionChoices(JPVisionSettings::Kind kind) const;
    std::vector<const JPPackage*>        packageChoices() const;

    // A column hidden until chosen (more about each part).
    static Column more(const char* name, const char* tooltip);
    // A multi-valued cell's values: each short (as the cell shows it) and in full (as its tooltip says it).
    std::vector<std::pair<std::string, std::string>> values(int row, int c) const;
    // The open boards using each row (by its place in the library), as last read.
    const std::vector<std::shared_ptr<JPBoard>>& usedBy(int row) const;

    JPConfiguration&                                   m_config;
    std::vector<std::vector<std::shared_ptr<JPBoard>>> m_usedBy;
    std::map<std::string, std::vector<JPStockLot>>     m_stock;   // each part's open lots, by its id, as last read
    std::string                                        m_show = kShowAll;
};

} // inline namespace jf
