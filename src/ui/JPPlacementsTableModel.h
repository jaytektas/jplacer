// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPTableModel.h"

#include "model/JPConfiguration.h"
#include "model/JPPartChoice.h"
#include "model/JPJob.h"

#include <functional>

inline namespace jf {

// A board's (or panel's) placements, as OpenPnP's
// PlacementsHolderPlacementsTableModel: Enabled, ID, Part, Side, X, Y, Rot.,
// Type, Placed, Status, Error Handling, Rank, Comments; a tab shows the
// columns it wants (the Boards tab leaves out Placed and Status). All but
// the ID and Status are edited in place, on the definition, and carried to
// its uses. IDs sort as reference designators (R2 before R10); a
// fiducial's type stands out.
class JPPlacementsTableModel : public JPTableModel {
public:
    enum Col { kEnabled, kId, kPart, kSide, kX, kY, kRotation, kType, kPlaced, kStatus, kErrorHandling, kRank,
               kComments, kVerified, kColumns };

    JPPlacementsTableModel(JPConfiguration& config, std::function<const JPJob*()> job, std::vector<Col> shown);

    std::function<void()> onChanged;

    // The holder whose placements are shown (a definition); null: none. A
    // panel's pseudo-placements follow its own placements, turned on and
    // off but not otherwise changed.
    void setHolder(JPPlacementsHolder* holder) {
        m_holder = holder;
        reload();
    }
    // A job's board or panel's placements (the Job tab): Placed kept by the
    // job; `editDefinition` (a board used once, straight in the job): every
    // column changes the board itself, else only Enabled and Error
    // Handling, on this use of it. Only the placements on its side facing up
    // are shown.
    void setLocation(JPPlacementsHolderLocation* location, bool editDefinition, JPJob* job);
    JPPlacementsHolderLocation* location() const { return m_location; }
    // Whether a feeder holds a part (Status's Missing Feeder).
    std::function<bool(const std::string& partId)> hasFeeder;
    // Opens the part picker for a board's placement; `chosen` has what was chosen. Set, a board's Part cell
    // opens it (a panel's keeps its list of the library's parts).
    std::function<void(JPBoard& board, const std::string& placementId, std::function<void(const JPPartChoice&)> chosen)>
        openPartPicker;
    // The pseudo-placements worked out again (before the table is shown again).
    void reload();
    bool isPseudo(int row) const;
    // Only Enabled changed in the table (a list to choose from).
    void setOnlyEnabledEditable(bool on) { m_onlyEnabled = on; }
    JPPlacementsHolder* holder() const { return m_holder; }
    JPPlacement* placement(int row) const;
    int          rowOf(const std::string& id) const;
    // The model column showing `c`, or -1.
    int columnOf(Col c) const;

    int    columnCount() const override { return int(m_shown.size()); }
    Column column(int c) const override;
    int    rowCount() const override;
    std::string text(int row, int c) const override;
    bool   checked(int row, int c) const override;
    double number(int row, int c) const override;
    std::string rowKey(int row) const override;
    std::optional<int> compare(int a, int b, int c) const override;
    std::string cellTooltip(int row, int c) const override;
    const uint8_t* cellTint(int row, int c) const override;
    bool   rowShown(int row) const override;
    bool   editable(int, int c) const override;
    std::vector<std::string> choices(int row, int c) const override;
    bool   setText(int row, int c, const std::string& text, std::string& error) override;
    void   setChoice(int row, int c, int index) override;
    // A part chosen for `placementId` of `board` (still the board edited), applied: its board part (or, the one
    // alone, one of its own) made that, every placement of it placed with it (JPDefinitionChanges).
    void applyPart(JPBoard* board, const std::string& placementId, const JPPartChoice& choice);
    void   pick(int row, int c) override;
    void   setChecked(int row, int c, bool on) override;

    // A job's placement marked placed or not.
    void setPlaced(int row, bool placed);
    // `set` applied to the placement `id`, on the definition and its uses.
    void edit(const std::string& id, const std::function<void(JPPlacement&)>& set);

    // OpenPnP's order for reference designators: those with no digit first,
    // then by the letters before the digits, then by the number.
    static int compareReferences(const std::string& a, const std::string& b);

    // OpenPnP's placement status, in its order.
    enum class Status { Ready, MissingPart, MissingFeeder, ZeroPartHeight, Disabled };
    Status status(const JPPlacement& p) const;
    static const char* statusName(Status s);

private:
    // The board definition an edit here changes (the board shown, or a job's board where its definition is
    // edited); null for a panel, or a job's use of a board edited alone.
    JPBoard* editedBoard() const;

    std::vector<const JPPart*> partChoices() const;

    JPConfiguration&              m_config;
    std::function<const JPJob*()> m_job;
    std::vector<Col>              m_shown;
    JPPlacementsHolder*           m_holder = nullptr;
    std::vector<JPPlacement>      m_pseudo;
    JPPlacementsHolderLocation*   m_location = nullptr;   // the job's, in the Job tab
    JPJob*                        m_placedJob = nullptr;
    bool                          m_editDefinition = true;
    bool                          m_onlyEnabled = false;
};

} // inline namespace jf
