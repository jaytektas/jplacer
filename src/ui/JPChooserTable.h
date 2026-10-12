// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPTable.h"
#include "JPTableModel.h"

#include "model/JPConfiguration.h"

#include <j/core/JComboBox.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

inline namespace jf {

// The table a choice of the library's parts or packages drops (a feeder's
// Part, the Parts tab's Package: JComboBox::Search::table): the Parts or
// Packages tab's own table, every column of it, sorted, moved and widened as
// there, read only, showing the entries the dropdown's filter and Show let
// through, in its order. It keeps its layout and its views as its own table
// (its state key), and puts its tools (Columns…, a button for each view:
// JPTableTools) at the end of the filter's row; its fields are not added or
// edited from it. A click on a
// row takes it; the keys walk it.
class JPChooserTable : public JComboBox::Search::Table {
public:
    // State keys: the part chooser's layout, the package chooser's.
    static constexpr const char* kParts = "chooser.parts";
    static constexpr const char* kPackages = "chooser.packages";

    // The dropdown's table maker for a choice whose entries are `keys` (part or package ids, "" none).
    using Maker = std::function<std::unique_ptr<JComboBox::Search::Table>(JSceneGraph&)>;
    static Maker parts(JPConfiguration& config, std::vector<std::string> keys);
    // The same for a choice that lists parts by their Names (a form's): each the id of the part of that Name.
    static Maker partsNamed(JPConfiguration& config, const std::vector<std::string>& names);
    static Maker packages(JPConfiguration& config, std::vector<std::string> keys);

    JPChooserTable(JSceneGraph& graph, std::unique_ptr<JPTableModel> source, std::vector<std::string> keys,
                   const std::string& stateKey);
    ~JPChooserTable() override;

    std::unique_ptr<JWidget> takeWidget() override;
    float width() const override;
    void  show(const std::vector<int>& entries, int current) override;
    int   chosen() const override;
    bool  key(const JKeyEvent& ke) override;
    std::unique_ptr<JWidget> barControl(JSceneGraph& graph) override;

private:
    class Rows;

    JSceneGraph&                  m_graph;
    std::unique_ptr<JPTableModel> m_source;
    std::unique_ptr<Rows>         m_rows;
    std::unique_ptr<JPTable>      m_owned;     // until the dropdown takes it
    JPTable*                      m_table = nullptr;
    bool                          m_quiet = false;   // the selection set here (filling, keys), not a click
};

} // inline namespace jf
