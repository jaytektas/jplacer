// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPBoard.h"
#include "JPBoardRevision.h"

#include <map>
#include <string>
#include <vector>

inline namespace jf {

// A board upgraded from a new revision's files (DESIGN.md, Board revisions): the new files' placements paired
// with the board's (by designator; then the rest by footprint and position, which is a renumbering), the
// board's own move found (most placements moved by one offset and turn: the CAD origin moved), and what is
// left sorted: unchanged, moved or turned, part changed, footprint changed, new, renamed, removed. The new
// revision keeps every decision that still holds: a placement's identity, what the job set for it, its
// rotation correction (as a difference from the CAD), its verified mark where nothing it was checked by
// changed, and a board part's choice where its line of the files is the same.
class JPBoardUpgrade {
public:
    // One placement, in the board as it is (`was`) and in the new files (`now`); one empty: new or removed.
    struct Row {
        std::string was;
        std::string now;
        bool        moved = false;              // where the CAD puts it (the board's move taken out)
        bool        turned = false;             // how the CAD turns it
        bool        partChanged = false;        // the same footprint, another part
        bool        footprintChanged = false;
        bool        toMatch = false;            // its part in the new revision is not chosen yet
        std::string detail;                     // what changed, in words ("moved 0.50 mm, turned 90°")
    };
    enum class Kind { Unchanged, Moved, PartChanged, FootprintChanged, New, Renamed, Removed };
    static constexpr Kind kKinds[] { Kind::Unchanged, Kind::Moved, Kind::PartChanged, Kind::FootprintChanged,
                                     Kind::New, Kind::Renamed, Kind::Removed };

    // `board`'s placements are given identities (JPBoard::giveIdentities) to be paired by. `files` is the
    // new files' board, as an import makes it (JPCplBomImport::build).
    JPBoardUpgrade(JPBoard& board, const JPBoard& files);

    const std::vector<Row>& rows() const { return m_rows; }
    static bool is(const Row& r, Kind k);
    size_t count(Kind k) const;
    // A kind counted, in words: "4 moved (to verify)".
    static std::string words(Kind k, size_t n);
    // Whether the CAD origin moved, and by how much (millimetres, degrees): the new files' placements are
    // taken back by it, so the board's place in a job and its fiducials still hold.
    bool   originMoved() const { return m_originMoved; }
    double originX() const { return m_dx; }
    double originY() const { return m_dy; }
    double originTurn() const { return m_turn; }
    // All of it in a line: "Origin moved 2.00, -1.50 mm; 182 unchanged, 4 moved (to verify), 1 new".
    std::string summary() const;
    // The new revision, labelled `label`, made `when`.
    JPBoardRevision revision(const std::string& label, const std::string& when) const;

private:
    struct Pair {
        const JPPlacement* was = nullptr;
        const JPPlacement* now = nullptr;
        size_t             row = 0;
    };
    void findOriginMove(const std::vector<Pair>& byDesignator);
    // A new placement where the board has it: the board's move taken out.
    JPPlacement inBoard(const JPPlacement& p) const;
    void sort(Pair& p);

    JPBoard&          m_board;
    const JPBoard&    m_files;
    // Each of the files' board parts the board's that is the same line of the files (its choice kept).
    std::map<std::string, const JPBoardPart*> m_same;
    std::vector<Pair> m_pairs;
    std::vector<Row>  m_rows;
    bool              m_originMoved = false;
    double            m_dx = 0, m_dy = 0, m_turn = 0;
};

} // inline namespace jf
