// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPTableModel.h"

#include "model/JPConfiguration.h"
#include "model/JPJob.h"
#include "model/JPPanelLocation.h"

#include <functional>
#include <vector>

inline namespace jf {

// Boards and panels where they lie, as OpenPnP's
// PlacementsHolderLocationsTableModel: Board/Panel Id, Name, Width,
// Length, Side, X, Y, Z, Rot., Enabled?, Check Fids?. A panel's children
// (the Panels tab, Z left out) or a job's boards and panels (the Job tab).
// Changes are made to the panel's definition and carried to its uses.
class JPLocationsTableModel : public JPTableModel {
public:
    enum Col { kId, kName, kWidth, kLength, kSide, kX, kY, kZ, kRotation, kEnabled, kCheckFids, kColumns };
    // PanelDefinition: a panel's own children, all but the size edited
    // (OpenPnP's PanelDefinitionPanel); Job: the job's, as its Job tab edits them.
    enum class Mode { PanelDefinition, Job };

    JPLocationsTableModel(JPConfiguration& config, Mode mode, std::vector<Col> shown,
                          std::function<const JPJob*()> job);

    std::function<void()> onChanged;

    // What is shown: the rows, and the panel (its location) they lie on.
    void setRows(JPPanelLocation* root, std::vector<JPPlacementsHolderLocation*> rows);
    JPPlacementsHolderLocation* location(int row) const;
    int rowOf(const JPPlacementsHolderLocation* l) const;

    int    columnCount() const override { return int(m_shown.size()); }
    Column column(int c) const override;
    int    rowCount() const override { return int(m_rows.size()); }
    std::string text(int row, int c) const override;
    bool   checked(int row, int c) const override;
    std::string rowKey(int row) const override;
    std::optional<int> compare(int a, int b, int c) const override;
    std::string displayText(int row, int c) const override;
    std::string cellIcon(int row, int c) const override;
    const uint8_t* cellTint(int row, int c) const override;
    std::string cellTooltip(int row, int c) const override;
    bool   editable(int row, int c) const override;
    std::vector<std::string> choices(int row, int c) const override;
    bool   setText(int row, int c, const std::string& text, std::string& error) override;
    void   setChoice(int row, int c, int index) override;
    void   setChecked(int row, int c, bool on) override;

    // `set` applied to the child at `row` and the same child of each use of
    // its panel (a change to where it lies, its side, enabled or fiducial check).
    void edit(JPPlacementsHolderLocation* l, const std::function<void(JPPlacementsHolderLocation&)>& set);
    // Its side as seen from the machine, keeping where it lies (OpenPnP's Side column).
    void setSide(JPPlacementsHolderLocation* l, JPSide side);

private:
    JPConfiguration&                          m_config;
    Mode                                      m_mode;
    std::vector<Col>                          m_shown;
    std::function<const JPJob*()>             m_job;
    JPPanelLocation*                          m_root = nullptr;
    std::vector<JPPlacementsHolderLocation*>  m_rows;
};

} // inline namespace jf
